// White-box test for server.js's 1 MB bufferedAmount backpressure ceiling
// (MAX_BUFFERED_BYTES, server.js:26; checked inside `send` at server.js:187-195) — a path
// no black-box test reaches, since it can only be crossed by traffic a *different*
// connection causes to be queued for a peer, never by anything the peer itself sends.
//
// createServer takes `room` as an injected dependency (server.js:48), so a fake room here
// drives one real target connection's `send` thousands of times from inside a single
// synchronous `handleMessage` call. Node does not flush a socket's queued writes until the
// current synchronous turn of the event loop ends, so a tight synchronous loop of sends
// reliably pushes the target's real `ws.bufferedAmount` (as tracked by the `ws` library
// itself, via the underlying Writable's buffered length) over the ceiling before any of
// those bytes actually leave the process — confirmed empirically against a bare
// WebSocketServer before writing this file. This stands in for a peer that stalls its
// reader: pausing a real client's socket read side was not needed, since the synchronous
// flood already saturates the write-side buffer before the target could read anything
// regardless of whether it is paused.

import test from 'node:test';
import assert from 'node:assert/strict';
import { WebSocket } from 'ws';

import { createServer } from '../src/server.js';
import { CLOSE_CODES } from '../src/protocol.js';

const RATE_LIMIT = { sustainedPerSecond: 1000, burst: 1000, banAfterMs: 60000 };
const FLOOD_COUNT = 10000; // empirically crosses the 1 MB ceiling around iteration 6000
const PAYLOAD = 'x'.repeat(500); // keeps each delta frame comfortably under MAX_FRAME_BYTES (4096)
const ONE_MEBIBYTE = 1024 * 1024;

function silentLogger() {
  const lines = [];
  const sink = (level) => (event, fields = {}) => {
    lines.push({ level, event, fields });
  };
  return { lines, error: sink('error'), warn: sink('warn'), info: sink('info'), debug: sink('debug') };
}

// A fake room, not the real one: room.js is irrelevant to this test, only server.js's own
// `send` closure is under test. The first message from any connection assigns conn.id
// (mirroring the only thing server.js itself reads off the result), and a 'flood' message
// calls the *target* connection's own send() many times synchronously — exercising the
// real server.js `send` closure repeatedly before this event-loop turn ends.
function floodRoom() {
  let target = null;
  return {
    handleMessage(conn, msg) {
      if (conn.id === null) {
        conn.id = msg.label;
        if (msg.label === 'target') target = conn;
        return { ok: true };
      }
      if (msg.type === 'flood') {
        for (let i = 0; i < msg.count; i += 1) {
          target.send({ type: 'delta', serverSeq: i, sourceClientId: 'x', deck: 'A', changes: { trackId: msg.payload } });
        }
        return { ok: true };
      }
      return { ok: true };
    },
    handleDisconnect() {},
  };
}

function connect(port) {
  const ws = new WebSocket(`ws://127.0.0.1:${port}`);
  return {
    ws,
    opened: new Promise((resolve) => ws.once('open', resolve)),
    closed: new Promise((resolve) => ws.once('close', (code) => resolve(code))),
    send(obj) {
      ws.send(JSON.stringify(obj));
    },
    dispose() {
      ws.removeAllListeners();
      ws.terminate();
    },
  };
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

async function startFloodServer() {
  const logger = silentLogger();
  const config = Object.freeze({
    host: '127.0.0.1',
    port: 0,
    roomCode: 'backpressure-room',
    maxClients: 8,
    logLevel: 'error',
    rateLimit: RATE_LIMIT,
  });
  const room = floodRoom();
  const server = await createServer({ config, logger, room });
  return { server, logger };
}

test('a target whose outbound buffer crosses 1 MB is terminated and the overrun is logged', async (t) => {
  const { server, logger } = await startFloodServer();
  const target = connect(server.port);
  const flooder = connect(server.port);
  t.after(async () => {
    target.dispose();
    flooder.dispose();
    await server.close();
  });

  await target.opened;
  await flooder.opened;
  target.send({ type: 'register', label: 'target' });
  flooder.send({ type: 'register', label: 'flooder' });

  const closedPromise = target.closed;
  flooder.send({ type: 'flood', count: FLOOD_COUNT, payload: PAYLOAD });

  const closeCode = await closedPromise;
  await waitFor(() => logger.lines.some((l) => l.event === 'backpressure-terminate'));

  const line = logger.lines.find((l) => l.event === 'backpressure-terminate');
  assert.ok(line, 'a backpressure-terminate line should be logged');
  assert.equal(line.level, 'warn');
  assert.ok(
    line.fields.bufferedAmount > ONE_MEBIBYTE,
    `the logged bufferedAmount (${line.fields.bufferedAmount}) should be past the 1 MB ceiling`,
  );
  // ws.terminate() tears the socket down without the closing handshake, so the target
  // never sees the graceful shutdown code — distinguishing this from server.close().
  assert.notEqual(closeCode, CLOSE_CODES.goingAway);
});

test('the flooding connection is not itself punished for its target stalling', async (t) => {
  const { server } = await startFloodServer();
  const target = connect(server.port);
  const flooder = connect(server.port);
  t.after(async () => {
    target.dispose();
    flooder.dispose();
    await server.close();
  });

  await target.opened;
  await flooder.opened;
  target.send({ type: 'register', label: 'target' });
  flooder.send({ type: 'register', label: 'flooder' });

  const targetClosed = target.closed;
  flooder.send({ type: 'flood', count: FLOOD_COUNT, payload: PAYLOAD });
  await targetClosed;

  assert.equal(flooder.ws.readyState, WebSocket.OPEN, 'the sender itself must stay connected');
});
