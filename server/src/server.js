// WebSocket transport for the sync room: framing, frame-size limits, the hello
// timer, the heartbeat, per-connection rate limiting and JSON parsing.
// All socket knowledge stops here — room.js only ever sees a parsed JSON value
// and a thin conn object. createServer resolves once the server is listening, so
// the `port` it hands back is the real bound port (config.port may be 0).

import { Buffer } from 'node:buffer';
import { WebSocket, WebSocketServer } from 'ws';
import {
  CLOSE_CODES,
  ERROR_CODES,
  HEARTBEAT_INTERVAL_MS,
  HELLO_TIMEOUT_MS,
  MAX_CONSECUTIVE_INVALID,
  MAX_FRAME_BYTES,
} from './protocol.js';

// Only a schema violation counts against the three-strike rule. An observer
// clicking play behaves exactly like a client with a stale UI: well-formed
// messages, legitimately rejected, and disconnecting it would be a bug.
const COUNTS_AS_INVALID = new Set([ERROR_CODES.badMessage]);

// A peer that drains its socket slowly but still answers pings never trips the
// heartbeat reaper, so without this ceiling it can make us queue unacknowledged
// writes in process memory indefinitely.
const MAX_BUFFERED_BYTES = 1024 * 1024;

// ws.close() only queues a close frame; it does not stop the receiver, and ws's own
// force-close fallback (closeTimeout) defaults to 30 s. That is long enough for a
// peer we just ejected (ban, three-strike, binary frame) to keep its socket, its
// slot, and graceful shutdown hostage. Force it well inside that window — long
// enough for a cooperative peer to receive the close code first (M7 renders it as
// user-facing text), short enough that an uncooperative one can't hold anything.
const EJECT_TERMINATE_GRACE_MS = 3000;

// maxClients is only checked once a hello validates, which leaves sockets that
// never speak unbounded — each one costs a descriptor, a timer and a heartbeat
// slot. Ceiling them at a multiple of the room size.
const PRE_HELLO_CEILING_FACTOR = 4;

// ws hands the message event a Buffer, an ArrayBuffer, or a list of fragments.
const decodeFrame = (data) => {
  if (Buffer.isBuffer(data)) return data.toString('utf8');
  if (Array.isArray(data)) return Buffer.concat(data).toString('utf8');
  return Buffer.from(data).toString('utf8');
};

/**
 * Binds a WebSocketServer for the given room and wires the connection lifecycle.
 * The room is injected rather than constructed here: it keeps this module free of
 * a room.js import and lets an integration test supply a room with a small cap.
 *
 * @returns {Promise<{wss: WebSocketServer, port: number, close: () => Promise<void>}>}
 *   resolved on `listening`, rejected if the bind fails (e.g. EADDRINUSE).
 */
export function createServer({ config, logger, room }) {
  const { rateLimit } = config;
  const records = new Set();
  const preHelloCeiling = config.maxClients * PRE_HELLO_CEILING_FACTOR;
  let preHelloCount = 0;

  // WebSocket handshakes are exempt from the same-origin policy, so any page an
  // operator happens to load can open a socket to loopback with no preflight.
  // Our client is IXWebSocket and sends no Origin, so refusing the header
  // outright costs nothing and closes every browser-driven path into the room.
  // The callback form is what gets a 403 on the wire — refusing by return value
  // makes ws answer 401, which invites a client to retry with credentials.
  const verifyClient = (info, done) => {
    if (info.req.headers.origin === undefined) {
      done(true);
      return;
    }
    // Never the origin value itself: it is attacker-controlled log content.
    logger.warn('origin-refused', { remoteAddress: info.req.socket.remoteAddress });
    done(false, 403, 'Forbidden');
  };

  const wss = new WebSocketServer({
    host: config.host,
    port: config.port,
    maxPayload: MAX_FRAME_BYTES,
    verifyClient,
  });

  // index.js exits the process on an uncaught exception, which is right for a
  // programmer error and catastrophic for client input. Every callback a frame
  // can reach runs inside this, contained to its own connection.
  const contain = (record, fn) => {
    try {
      fn();
    } catch (err) {
      logger.error('handler-error', { clientId: record.conn.id, message: err?.message });
      record.conn.close(CLOSE_CODES.internalError, 'internal error');
    }
  };

  const refill = (record, now) => {
    const elapsedMs = now - record.lastRefillMs;
    if (elapsedMs <= 0) return;
    record.lastRefillMs = now;
    record.tokens = Math.min(
      rateLimit.burst,
      record.tokens + (elapsedMs / 1000) * rateLimit.sustainedPerSecond,
    );
  };

  // Charges one token; false means the frame must be dropped. A deficit episode
  // produces exactly one reply and one log line — answering every dropped frame
  // would make the limiter send more bytes than it receives and turn it into the
  // flood's amplifier. The episode only closes once the bucket has recovered to
  // half of burst, otherwise a flooder pausing for a single refill would reset
  // the ban clock forever.
  const charge = (record) => {
    const now = Date.now();
    refill(record, now);
    const served = record.tokens >= 1;
    if (served) record.tokens -= 1;

    if (served) {
      if (record.rateLimitedSince !== null && record.tokens >= rateLimit.burst / 2) {
        record.rateLimitedSince = null;
      }
    } else if (record.rateLimitedSince === null) {
      record.rateLimitedSince = now;
      logger.warn('rate-limited', {
        clientId: record.conn.id,
        remoteAddress: record.conn.remoteAddress,
      });
      record.conn.send({
        type: 'error',
        code: ERROR_CODES.rateLimited,
        message: 'too many messages',
      });
    }

    if (record.rateLimitedSince !== null && now - record.rateLimitedSince > rateLimit.banAfterMs) {
      logger.warn('rate-limit-ban', {
        clientId: record.conn.id,
        remoteAddress: record.conn.remoteAddress,
      });
      record.conn.close(CLOSE_CODES.rateLimitBan, 'rate limit');
      return false;
    }
    return served;
  };

  const countInvalid = (record) => {
    record.consecutiveInvalid += 1;
    if (record.consecutiveInvalid < MAX_CONSECUTIVE_INVALID) return;
    // clientId is null for a connection that never completed a hello, which is
    // exactly the case where an operator needs the address instead.
    logger.warn('too-many-invalid', {
      clientId: record.conn.id,
      remoteAddress: record.conn.remoteAddress,
    });
    record.conn.close(CLOSE_CODES.policyViolation, 'too many invalid messages');
  };

  // Idempotent: `pendingHello` guards the decrement, so this is safe to call both
  // from close() (the moment a rejection is decided) and again from the socket's
  // close event (the fallback for any close that doesn't route through close()).
  const releaseHelloWatch = (record) => {
    if (record.helloTimer !== null) {
      clearTimeout(record.helloTimer);
      record.helloTimer = null;
    }
    if (record.pendingHello) {
      record.pendingHello = false;
      preHelloCount -= 1;
    }
  };

  const onConnection = (ws, req) => {
    const remoteAddress = req.socket.remoteAddress;

    if (preHelloCount >= preHelloCeiling) {
      logger.warn('pre-hello-ceiling', { remoteAddress });
      // An unhandled 'error' event on an EventEmitter throws, so this socket
      // needs a listener even though we are dropping it.
      ws.on('error', () => {});
      ws.close(CLOSE_CODES.policyViolation, 'too many pending connections');
      return;
    }

    const close = (code, reason) => {
      if (record.ejected) return;
      record.ejected = true;
      // The pre-hello phase is over the moment we decide to eject, win or lose;
      // freeing the slot here (instead of only on the socket's eventual close
      // event) is what keeps an uncooperative pre-hello peer from holding it for
      // the full grace window below.
      releaseHelloWatch(record);
      try {
        ws.close(code, reason);
      } catch {
        // A socket that is already gone is not a failure to report.
      }
      record.terminateTimer = setTimeout(() => ws.terminate(), EJECT_TERMINATE_GRACE_MS);
      record.terminateTimer.unref();
    };

    // Total by contract: it never throws for any input, so callers — the room in
    // particular — are told to rely on it instead of wrapping their own catch.
    const send = (obj) => {
      try {
        if (ws.readyState !== WebSocket.OPEN) return;
        if (ws.bufferedAmount > MAX_BUFFERED_BYTES) {
          logger.warn('backpressure-terminate', {
            clientId: conn.id,
            remoteAddress,
            bufferedAmount: ws.bufferedAmount,
          });
          ws.terminate();
          return;
        }
        const text = JSON.stringify(obj);
        const bytes = Buffer.byteLength(text, 'utf8');
        if (bytes > MAX_FRAME_BYTES) {
          // Dropping it would leave the peer connected past its own handshake
          // with default state and no error. A frame we cannot put on the wire
          // means our state is unrepresentable — a programmer error, so fail
          // loud and let the client reconnect.
          logger.error('oversize-outbound', { type: obj?.type, bytes });
          close(CLOSE_CODES.internalError, 'internal error');
          return;
        }
        ws.send(text);
      } catch {
        logger.error('send-failed', { type: obj?.type });
      }
    };

    // No role field: the room derives role from its own controllerId, and a
    // second copy here would be an unenforced two-writer invariant.
    const conn = { id: null, name: null, send, close, remoteAddress };

    const record = {
      ws,
      conn,
      helloTimer: null,
      terminateTimer: null,
      pendingHello: true,
      tokens: rateLimit.burst,
      lastRefillMs: Date.now(),
      rateLimitedSince: null,
      consecutiveInvalid: 0,
      isAlive: true,
      ejected: false,
    };
    preHelloCount += 1;
    records.add(record);

    record.helloTimer = setTimeout(() => {
      contain(record, () => {
        record.helloTimer = null;
        if (conn.id !== null) return;
        logger.info('hello-timeout', { remoteAddress });
        close(CLOSE_CODES.badHandshake, 'hello timeout');
      });
    }, HELLO_TIMEOUT_MS);

    ws.on('message', (data, isBinary) => {
      contain(record, () => {
        // close() has already decided this connection is done; ws keeps delivering
        // events until the close handshake (or our terminate fallback) completes,
        // so every handler must refuse to act on its behalf in the meantime.
        if (record.ejected) return;
        if (isBinary) {
          logger.warn('binary-frame', { clientId: conn.id, remoteAddress });
          close(CLOSE_CODES.policyViolation, 'text frames only');
          return;
        }
        if (!charge(record)) return;

        let parsed;
        try {
          parsed = JSON.parse(decodeFrame(data));
        } catch {
          send({ type: 'error', code: ERROR_CODES.badMessage, message: 'malformed json' });
          countInvalid(record);
          return;
        }

        const result = room.handleMessage(conn, parsed);
        if (conn.id !== null) releaseHelloWatch(record);

        if (result?.ok) {
          record.consecutiveInvalid = 0;
          return;
        }
        // No errorCode means the room closed the connection itself.
        if (result?.errorCode && COUNTS_AS_INVALID.has(result.errorCode)) countInvalid(record);
      });
    });

    // ws answers an inbound ping with a pong automatically, so control frames
    // cost a parse and a write each; charge them too or a ping flood is free.
    // A conforming client pings once every 15 s, well under the bucket.
    ws.on('ping', () => {
      contain(record, () => {
        if (record.ejected) return;
        charge(record);
      });
    });

    ws.on('pong', () => {
      contain(record, () => {
        if (record.ejected) return;
        record.isAlive = true;
        charge(record);
      });
    });

    ws.on('error', (err) => {
      contain(record, () => {
        logger.warn('connection-error', { clientId: conn.id, remoteAddress, message: err?.message });
      });
    });

    // Teardown belongs here and not in the error handler: ws fires close after
    // error in every case, and doing both would double-fire peerLeft.
    ws.on('close', (code) => {
      contain(record, () => {
        if (code === CLOSE_CODES.oversizeFrame) {
          logger.warn('oversize-frame', { clientId: conn.id, remoteAddress });
        }
        // The socket is gone; a pending fallback terminate() would be a no-op at
        // best, but leaving it armed is a dangling timer for no reason.
        if (record.terminateTimer !== null) {
          clearTimeout(record.terminateTimer);
          record.terminateTimer = null;
        }
        releaseHelloWatch(record);
        records.delete(record);
        room.handleDisconnect(conn);
      });
    });
  };

  wss.on('connection', onConnection);

  const heartbeat = setInterval(() => {
    for (const record of records) {
      // Per iteration, so one bad connection cannot abort the sweep.
      contain(record, () => {
        if (!record.isAlive) {
          logger.info('heartbeat-timeout', {
            clientId: record.conn.id,
            remoteAddress: record.conn.remoteAddress,
          });
          record.ws.terminate();
          return;
        }
        record.isAlive = false;
        record.ws.ping();
      });
    }
  }, HEARTBEAT_INTERVAL_MS);
  heartbeat.unref();

  const close = () =>
    new Promise((resolve) => {
      clearInterval(heartbeat);
      for (const record of records) {
        releaseHelloWatch(record);
        record.conn.close(CLOSE_CODES.goingAway, 'server shutting down');
      }
      wss.close(() => resolve());
    });

  return new Promise((resolve, reject) => {
    const onStartupError = (err) => {
      logger.error('server-error', { code: err?.code, message: err?.message });
      clearInterval(heartbeat);
      reject(err);
    };

    wss.once('error', onStartupError);
    wss.once('listening', () => {
      wss.off('error', onStartupError);
      wss.on('error', (err) => {
        logger.error('server-error', { code: err?.code, message: err?.message });
      });
      const { port } = wss.address();
      // 06-security.md §Server rules 3: the startup log states the bind address.
      logger.info('listening', { host: config.host, port });
      resolve({ wss, port, close });
    });
  });
}
