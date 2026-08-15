// White-box tests for two things server.js owns that room.js's own contract
// (room.test.js) cannot see, per 03-server.md §9:
//
//  1. The mapping from a domain fact — {ok:false, errorCode} — to the three-strike close.
//     COUNTS_AS_INVALID (server.js:21) is not exported, so it is exercised here through
//     server.js's real behaviour with a scripted fake room standing in for room.js.
//  2. Containment: every socket callback is wrapped in `contain()` (server.js:80-87), so a
//     throw anywhere inside room.js must close only the one connection that triggered it,
//     with 1011, and must never escape to crash the process.
//
// createServer takes `room` as an injected dependency (server.js:48), which is what makes
// both of these testable without needing room.js's own protocol semantics to hold.

import test from 'node:test';
import assert from 'node:assert/strict';
import { WebSocket } from 'ws';

import { createServer } from '../src/server.js';
import { ERROR_CODES, CLOSE_CODES } from '../src/protocol.js';

const RATE_LIMIT = { sustainedPerSecond: 1000, burst: 1000, banAfterMs: 60000 };

function silentLogger() {
  const lines = [];
  const sink = (level) => (event, fields = {}) => {
    lines.push({ level, event, fields });
  };
  return { lines, error: sink('error'), warn: sink('warn'), info: sink('info'), debug: sink('debug') };
}

// A minimal stand-in for room.js: the first message from any connection "completes the
// handshake" by assigning conn.id (the only thing server.js itself reads off the room's
// return value or connection state), and every later message is answered by `script`.
function scriptedRoom(script) {
  let counter = 0;
  return {
    handleMessage(conn, msg) {
      if (conn.id === null) {
        conn.id = msg.label ?? `c-${(counter += 1)}`;
        return { ok: true };
      }
      return script(conn, msg);
    },
    handleDisconnect() {},
  };
}

async function startFakeServer(room, { maxClients = 8, rateLimit = RATE_LIMIT } = {}) {
  const logger = silentLogger();
  const config = Object.freeze({
    host: '127.0.0.1',
    port: 0,
    roomCode: 'seam-room',
    maxClients,
    logLevel: 'error',
    rateLimit,
  });
  const server = await createServer({ config, logger, room });
  return { server, logger, port: server.port };
}

function connect(port) {
  const ws = new WebSocket(`ws://127.0.0.1:${port}`);
  const inbox = [];
  const waiters = [];
  const client = {
    ws,
    opened: new Promise((resolve) => ws.once('open', resolve)),
    closed: new Promise((resolve) => {
      ws.once('close', (code, reason) => resolve({ code, reason: reason?.toString() ?? '' }));
    }),
    send(obj) {
      ws.send(JSON.stringify(obj));
    },
    next(ms = 3000) {
      if (inbox.length > 0) return Promise.resolve(inbox.shift());
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('timed out waiting for a frame')), ms);
        waiters.push({ resolve, reject, timer });
      });
    },
    dispose() {
      ws.removeAllListeners('message');
      ws.terminate();
    },
  };
  ws.on('message', (data) => {
    const parsed = JSON.parse(data.toString());
    const waiter = waiters.shift();
    if (waiter) {
      clearTimeout(waiter.timer);
      waiter.resolve(parsed);
    } else {
      inbox.push(parsed);
    }
  });
  return client;
}

async function open(client) {
  await client.opened;
}

async function expectClose(client, ms = 3000) {
  return Promise.race([
    client.closed,
    new Promise((_, reject) => {
      const timer = setTimeout(() => reject(new Error('timed out waiting for the socket to close')), ms);
      if (typeof timer.unref === 'function') timer.unref();
    }),
  ]);
}

async function waitFor(conditionFn, { timeoutMs = 3000, intervalMs = 20 } = {}) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (conditionFn()) return;
    await new Promise((resolve) => {
      setTimeout(resolve, intervalMs);
    });
  }
  throw new Error('condition not met before timeout');
}

// ---------------------------------------------------------------------------
// COUNTS_AS_INVALID mapping
// ---------------------------------------------------------------------------

test('three consecutive bad-message rejections from the room trip the three-strike close', async (t) => {
  const room = scriptedRoom(() => ({ ok: false, errorCode: ERROR_CODES.badMessage }));
  const { server, port } = await startFakeServer(room);
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'register' });
  client.send({ type: 'x' });
  client.send({ type: 'x' });
  client.send({ type: 'x' });

  assert.equal((await expectClose(client)).code, CLOSE_CODES.policyViolation);
});

for (const [label, code] of [
  ['control-taken', ERROR_CODES.controlTaken],
  ['not-controller', ERROR_CODES.notController],
  ['rate-limited', ERROR_CODES.rateLimited],
  ['unknown-track', ERROR_CODES.unknownTrack],
]) {
  test(`three consecutive ${label} rejections from the room do not trip the three-strike close`, async (t) => {
    const room = scriptedRoom((conn, msg) => {
      if (msg.type === 'probe') {
        conn.send({ type: 'probe-ack' });
        return { ok: true };
      }
      return { ok: false, errorCode: code };
    });
    const { server, port } = await startFakeServer(room);
    const client = connect(port);
    t.after(async () => {
      client.dispose();
      await server.close();
    });

    await open(client);
    client.send({ type: 'register' });
    client.send({ type: 'x' });
    client.send({ type: 'x' });
    client.send({ type: 'x' });
    client.send({ type: 'probe' });

    const ack = await client.next();
    assert.equal(ack.type, 'probe-ack', 'the connection should still be alive and processing messages');
  });
}

test('an ok:false result with no errorCode never counts toward a strike, however many times it repeats', async (t) => {
  // Mirrors a handshake failure or capacity refusal: room.js already closed the connection
  // itself in that case, so there is nothing left to strike — server.js:265-266 gates
  // countInvalid on `result?.errorCode` being present at all, not just on its value.
  const room = scriptedRoom((conn, msg) => {
    if (msg.type === 'probe') {
      conn.send({ type: 'probe-ack' });
      return { ok: true };
    }
    return { ok: false };
  });
  const { server, port, logger } = await startFakeServer(room);
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'register' });
  client.send({ type: 'x' });
  client.send({ type: 'x' });
  client.send({ type: 'x' });
  client.send({ type: 'probe' });

  const ack = await client.next();
  assert.equal(ack.type, 'probe-ack');
  assert.equal(logger.lines.some((l) => l.event === 'too-many-invalid'), false);
});

// ---------------------------------------------------------------------------
// containment: a throw inside room.js must not escape contain()
// ---------------------------------------------------------------------------

test('a throw inside room.handleMessage closes only that connection with 1011 and is logged', async (t) => {
  const room = scriptedRoom((conn, msg) => {
    if (msg.type === 'boom') throw new Error('room exploded');
    return { ok: true };
  });
  const { server, port, logger } = await startFakeServer(room);
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'register' });
  client.send({ type: 'boom' });

  assert.equal((await expectClose(client)).code, CLOSE_CODES.internalError);
  const line = logger.lines.find((l) => l.event === 'handler-error');
  assert.ok(line, 'a handler-error line should be logged');
  assert.equal(line.level, 'error');
  assert.equal(line.fields.message, 'room exploded');
});

test('a throw on one connection does not affect a second, unrelated connection', async (t) => {
  const room = scriptedRoom((conn, msg) => {
    if (msg.type === 'boom') throw new Error('room exploded');
    if (msg.type === 'probe') conn.send({ type: 'probe-ack' });
    return { ok: true };
  });
  const { server, port } = await startFakeServer(room);
  const bad = connect(port);
  const good = connect(port);
  t.after(async () => {
    bad.dispose();
    good.dispose();
    await server.close();
  });

  await open(bad);
  await open(good);
  bad.send({ type: 'register' });
  good.send({ type: 'register' });
  bad.send({ type: 'boom' });
  await expectClose(bad);

  good.send({ type: 'probe' });
  const ack = await good.next();
  assert.equal(ack.type, 'probe-ack');
  assert.equal(good.ws.readyState, WebSocket.OPEN);
});

test('a throw inside room.handleDisconnect while tearing down one connection does not affect another', async (t) => {
  const room = {
    handleMessage(conn, msg) {
      if (conn.id === null) {
        conn.id = msg.label;
        return { ok: true };
      }
      if (msg.type === 'probe') conn.send({ type: 'probe-ack' });
      return { ok: true };
    },
    handleDisconnect(conn) {
      if (conn.id === 'doomed') throw new Error('disconnect exploded');
    },
  };
  const { server, port, logger } = await startFakeServer(room);
  const doomed = connect(port);
  const survivor = connect(port);
  t.after(async () => {
    doomed.dispose();
    survivor.dispose();
    await server.close();
  });

  await open(doomed);
  await open(survivor);
  doomed.send({ type: 'register', label: 'doomed' });
  survivor.send({ type: 'register', label: 'survivor' });

  doomed.dispose(); // forces a real 'close' event on the server side, which drives handleDisconnect
  await waitFor(() => logger.lines.some((l) => l.event === 'handler-error'));

  const line = logger.lines.find((l) => l.event === 'handler-error');
  assert.equal(line.fields.message, 'disconnect exploded');

  survivor.send({ type: 'probe' });
  const ack = await survivor.next();
  assert.equal(ack.type, 'probe-ack', 'a throwing disconnect handler for one client must not affect another');
});
