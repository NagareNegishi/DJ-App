// White-box tests that need a peer that does not behave like a conforming WebSocket
// client: a real `ws`/WebSocket instance refuses to `send()` the instant it observes
// CLOSING, and it always answers a server's close frame with its own, closing the TCP
// connection on the next loopback round trip. Both facts make certain server.js paths
// unreachable through it:
//
//  - the ejection guard (`record.ejected`, server.js:190-191, 271, 312, 319): proving a
//    frame sent *after* the server decided to eject a connection never reaches the room
//    needs a peer that can still write to the socket at that point;
//  - the pre-hello slot release (server.js:196, inside `close()`): proving the slot frees
//    the instant close() runs, not only once the socket's own 'close' event later fires,
//    needs a peer that never completes the closing handshake at all, so the two moments
//    stay far enough apart in time to tell apart.
//
// test/helpers/rawWsClient.js does the WebSocket opening handshake and frame masking by
// hand over a plain net.Socket so these tests can hold a connection open (or keep writing
// to it) past the point where a real client would have already torn it down.

import test from 'node:test';
import assert from 'node:assert/strict';
import { WebSocket } from 'ws';

import { createServer } from '../src/server.js';
import { createRoom } from '../src/room.js';
import { connectRaw, OPCODE } from './helpers/rawWsClient.js';

const ROOM_CODE = 'raw-frame-room-code';
const RATE_LIMIT = { sustainedPerSecond: 1000, burst: 1000, banAfterMs: 60000 };

function silentLogger() {
  const lines = [];
  const sink = (level) => (event, fields = {}) => {
    lines.push({ level, event, fields });
  };
  return { lines, error: sink('error'), warn: sink('warn'), info: sink('info'), debug: sink('debug') };
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

const delay = (ms) => new Promise((resolve) => {
  setTimeout(resolve, ms);
});

// A minimal stand-in for room.js, in the style of server.failure-containment.test.js: the
// first message from a connection "completes the handshake" by assigning conn.id, and a
// 'probe' message afterwards is counted and acknowledged. Counting on the room side, not
// just watching for an absence of frames on the wire, is what makes "the frame never
// reached the room" a positive assertion instead of a timing-sensitive negative one.
function countingRoom() {
  let probes = 0;
  return {
    room: {
      handleMessage(conn, msg) {
        if (conn.id === null) {
          conn.id = msg.label;
          return { ok: true };
        }
        if (msg.type === 'probe') {
          probes += 1;
          conn.send({ type: 'probe-ack' });
        }
        return { ok: true };
      },
      handleDisconnect() {},
    },
    probeCount: () => probes,
  };
}

async function startFakeServer(room, { maxClients = 8 } = {}) {
  const logger = silentLogger();
  const config = Object.freeze({
    host: '127.0.0.1',
    port: 0,
    roomCode: ROOM_CODE,
    maxClients,
    logLevel: 'error',
    rateLimit: RATE_LIMIT,
  });
  const server = await createServer({ config, logger, room });
  return { server, logger, port: server.port };
}

async function startRealServer({ maxClients = 8 } = {}) {
  const logger = silentLogger();
  const config = Object.freeze({
    host: '127.0.0.1',
    port: 0,
    roomCode: ROOM_CODE,
    maxClients,
    logLevel: 'error',
    rateLimit: RATE_LIMIT,
  });
  const room = createRoom({ roomCode: ROOM_CODE, maxClients, logger, protocolVersion: 1 });
  const server = await createServer({ config, logger, room });
  return { server, logger, port: server.port };
}

// ---------------------------------------------------------------------------
// the ejection guard
// ---------------------------------------------------------------------------

test('a frame sent after the server has already ejected the connection never reaches the room', async (t) => {
  const { room, probeCount } = countingRoom();
  const { server, port } = await startFakeServer(room);
  const raw = await connectRaw(port);
  t.after(async () => {
    raw.dispose();
    await server.close();
  });

  raw.sendText({ type: 'register', label: 'raw' });
  // A binary frame is server.js's own policy-violation ejection (server.js:272-276): it
  // sets record.ejected before this handler returns, without ever reaching the room.
  raw.sendBinary(Buffer.from([0x01, 0x02, 0x03]));
  // Sent on the same still-open TCP connection immediately afterwards. A conforming
  // client could never do this — it would already refuse to send() — which is exactly
  // why the guard is needed and exactly why this test cannot use one.
  raw.sendText({ type: 'probe' });

  await waitFor(() => raw.frames.some((f) => f.opcode === OPCODE.close));
  await delay(200); // give a wrongly-processed probe every chance to arrive

  assert.equal(probeCount(), 0, 'the room must never see a message sent after ejection');
  assert.equal(
    raw.frames.some((f) => f.opcode === OPCODE.text && f.payload.toString('utf8').includes('probe-ack')),
    false,
    'no probe-ack should have been written back either',
  );
});

test('a ping sent after the server has already ejected the connection is not charged or answered', async (t) => {
  // ws.on('ping', ...) has the same `if (record.ejected) return;` guard (server.js:311-315)
  // as the message handler; charge() must not run for it either.
  const { room, probeCount } = countingRoom();
  const { server, port } = await startFakeServer(room);
  const raw = await connectRaw(port);
  t.after(async () => {
    raw.dispose();
    await server.close();
  });

  raw.sendText({ type: 'register', label: 'raw' });
  raw.sendBinary(Buffer.from([0x01]));
  raw.sendRawBytes(OPCODE.ping, Buffer.alloc(0));
  raw.sendText({ type: 'probe' });

  await waitFor(() => raw.frames.some((f) => f.opcode === OPCODE.close));
  await delay(200);

  assert.equal(probeCount(), 0);
});

// ---------------------------------------------------------------------------
// pre-hello slot release
// ---------------------------------------------------------------------------

test('a rejected pre-hello connection frees its slot immediately, even when its socket never actually closes', async (t) => {
  // The ceiling is maxClients * 4 sockets that have not yet completed a hello
  // (PRE_HELLO_CEILING_FACTOR, server.js:39). Fill it, reject exactly one of those
  // connections, and — critically — never let that one complete a close handshake or
  // otherwise go away. If the slot were freed only on the socket's eventual 'close' event
  // (the pre-fix behaviour), a fresh connection right after would still be turned away.
  const { server, port } = await startRealServer({ maxClients: 1 });
  const filling = [];
  let fresh = null;
  t.after(async () => {
    for (const client of filling) client.dispose?.();
    if (fresh) fresh.terminate();
    await server.close();
  });

  const raw = await connectRaw(port);
  filling.push(raw);
  for (let i = 0; i < 3; i += 1) {
    const client = new WebSocket(`ws://127.0.0.1:${port}`);
    await new Promise((resolve, reject) => {
      client.once('open', resolve);
      client.once('error', reject);
    });
    filling.push({ dispose: () => client.terminate() });
  }

  // Any first message other than a well-formed hello draws close 4000 (badHandshake,
  // server.js:286-289 for a JSON.parse failure, or room.js's own closeHandshake for a
  // well-formed-but-wrong-type message). Sent and then left entirely alone: no reply is
  // read, no close frame is answered, the raw TCP connection is simply never touched
  // again until this test's own teardown destroys it.
  raw.sendText({ type: 'not-a-hello' });
  await waitFor(() => raw.frames.some((f) => f.opcode === OPCODE.close), { timeoutMs: 2000 });

  fresh = new WebSocket(`ws://127.0.0.1:${port}`);
  const welcome = await new Promise((resolve, reject) => {
    fresh.once('open', () => {
      fresh.send(JSON.stringify({ type: 'hello', protocolVersion: 1, name: 'nagare', room: ROOM_CODE }));
    });
    fresh.once('message', (data) => resolve(JSON.parse(data.toString())));
    fresh.once('close', (code) => reject(new Error(`fresh connection was closed instead of admitted (${code})`)));
    fresh.once('error', reject);
  });

  assert.equal(welcome.type, 'welcome', 'the freed slot should have let a new connection in');
