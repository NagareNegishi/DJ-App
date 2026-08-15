// Integration tests: a real WebSocketServer on an ephemeral port with real ws clients,
// per 05-testing.md. Heartbeat termination is deliberately not covered here (timer-based,
// covered by code review per the same document).

import test from 'node:test';
import assert from 'node:assert/strict';
import { WebSocket } from 'ws';

import { createServer } from '../src/server.js';
import { createRoom } from '../src/room.js';

const ROOM_CODE = 'integration-room-code';
const HELLO_TIMEOUT_MS = 5000;

const DECK_DEFAULTS = {
  trackId: null,
  playing: false,
  positionSeconds: 0,
  gain: 1.0,
  playbackRate: 1.0,
  pitchOffsetSemitones: 0,
  loop: null,
};

const DEFAULT_RATE_LIMIT = { sustainedPerSecond: 60, burst: 120, banAfterMs: 5000 };

const delay = (ms) => new Promise((resolve) => {
  setTimeout(resolve, ms);
});

function silentLogger() {
  const lines = [];
  const sink = (level) => (event, fields = {}) => {
    lines.push({ level, event, fields });
  };
  return {
    lines,
    error: sink('error'),
    warn: sink('warn'),
    info: sink('info'),
    debug: sink('debug'),
  };
}

// createServer may either return a promise or expose a `ready` promise; both are allowed
// by the server-net spec, so accept either shape here.
async function startServer({ maxClients = 8, rateLimit = DEFAULT_RATE_LIMIT, roomCode = ROOM_CODE } = {}) {
  const logger = silentLogger();
  const config = Object.freeze({
    host: '127.0.0.1',
    port: 0,
    roomCode,
    maxClients,
    logLevel: 'error',
    rateLimit,
  });
  const room = createRoom({ roomCode, maxClients, logger, protocolVersion: 1 });
  const server = await createServer({ config, logger, room });
  if (server && typeof server.ready?.then === 'function') await server.ready;
  return { server, room, logger, port: server.port };
}

function connect(port, options = {}) {
  const ws = new WebSocket(`ws://127.0.0.1:${port}`, options);
  const inbox = [];
  const waiters = [];
  const errors = [];

  const client = {
    ws,
    inbox,
    errors,
    opened: new Promise((resolve) => {
      ws.once('open', () => resolve('open'));
    }),
    failed: new Promise((resolve) => {
      ws.once('error', (err) => resolve(err));
    }),
    closed: new Promise((resolve) => {
      ws.once('close', (code, reason) => resolve({ code, reason: reason?.toString() ?? '' }));
    }),
    send(obj) {
      ws.send(JSON.stringify(obj));
    },
    sendRaw(data, options2) {
      ws.send(data, options2);
    },
    next(ms = 3000) {
      if (inbox.length > 0) return Promise.resolve(inbox.shift());
      return new Promise((resolve, reject) => {
        const waiter = { resolve, reject, timer: null };
        waiter.timer = setTimeout(() => {
          const index = waiters.indexOf(waiter);
          if (index >= 0) waiters.splice(index, 1);
          reject(new Error('timed out waiting for a frame'));
        }, ms);
        waiters.push(waiter);
      });
    },
    dispose() {
      for (const waiter of waiters.splice(0)) clearTimeout(waiter.timer);
      ws.removeAllListeners('message');
      ws.terminate();
    },
  };

  ws.on('error', (err) => errors.push(err));
  ws.on('message', (data) => {
    let parsed;
    try {
      parsed = JSON.parse(data.toString());
    } catch {
      parsed = { type: '<unparsable>', raw: data.toString() };
    }
    const waiter = waiters.shift();
    if (waiter) {
      clearTimeout(waiter.timer);
      waiter.resolve(parsed);
    } else {
      inbox.push(parsed);
    }
  });
  ws.on('close', () => {
    for (const waiter of waiters.splice(0)) {
      clearTimeout(waiter.timer);
      waiter.reject(new Error('socket closed while waiting for a frame'));
    }
  });

  return client;
}

async function open(client, ms = 3000) {
  const timeout = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for the socket to open')), ms);
    if (typeof timer.unref === 'function') timer.unref();
  });
  await Promise.race([
    client.opened,
    client.failed.then((err) => {
      throw err;
    }),
    timeout,
  ]);
  assert.equal(client.ws.readyState, WebSocket.OPEN, 'socket should be open');
}

async function handshake(client, name = 'nagare', room = ROOM_CODE) {
  await open(client);
  client.send({ type: 'hello', protocolVersion: 1, name, room });
  const welcome = await client.next();
  assert.equal(welcome.type, 'welcome', `expected a welcome, got ${welcome.type}`);
  return welcome;
}

async function nextOfType(client, type, ms = 3000) {
  for (let attempt = 0; attempt < 20; attempt += 1) {
    const frame = await client.next(ms);
    if (frame.type === type) return frame;
  }
  throw new Error(`no ${type} frame arrived`);
}

async function expectClose(client, ms = 3000) {
  const timeout = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for the socket to close')), ms);
    if (typeof timer.unref === 'function') timer.unref();
  });
  return Promise.race([client.closed, timeout]);
}

// ---------------------------------------------------------------------------
// startup
// ---------------------------------------------------------------------------

test('createServer reports the port it actually bound, not the zero it was configured with', async (t) => {
  const { server } = await startServer();
  t.after(async () => {
    await server.close();
  });

  assert.equal(typeof server.port, 'number');
  assert.ok(server.port > 0, 'an ephemeral port should have been resolved');
  assert.equal(server.wss.address().port, server.port);
});

// ---------------------------------------------------------------------------
// handshake
// ---------------------------------------------------------------------------

test('a valid hello over a real socket draws a welcome with the default deck A snapshot', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  const welcome = await handshake(client, 'nagare');

  assert.match(welcome.clientId, /^c-[0-9a-f]{4}$/);
  assert.equal(welcome.role, 'observer');
  assert.ok(Number.isInteger(welcome.serverSeq));
  assert.deepEqual(welcome.snapshot, { decks: { A: DECK_DEFAULTS } });
  assert.deepEqual(welcome.peers, []);
});

test('a hello quoting the wrong room code closes 4004', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'hello', protocolVersion: 1, name: 'nagare', room: 'not-the-room-code' });

  assert.equal((await expectClose(client)).code, 4004);
});

test('a hello declaring an unspoken protocol version closes 4001', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'hello', protocolVersion: 99, name: 'nagare', room: ROOM_CODE });

  assert.equal((await expectClose(client)).code, 4001);
});

test('a hello with no protocolVersion field closes 4001 rather than 4000', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'hello', name: 'nagare', room: ROOM_CODE });

  assert.equal((await expectClose(client)).code, 4001);
});

test('a first message that is not a hello closes 4000', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);
  client.send({ type: 'claimControl' });

  assert.equal((await expectClose(client)).code, 4000);
});

test('a hello arriving at a full room closes 4002', async (t) => {
  const { server, port } = await startServer({ maxClients: 1 });
  const first = connect(port);
  const second = connect(port);
  t.after(async () => {
    first.dispose();
    second.dispose();
    await server.close();
  });

  await handshake(first, 'aki');
  await open(second);
  second.send({ type: 'hello', protocolVersion: 1, name: 'nagare', room: ROOM_CODE });

  assert.equal((await expectClose(second)).code, 4002);
});

test('a connection that never says hello is closed 4000 by the handshake timer', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await open(client);

  assert.equal((await expectClose(client, HELLO_TIMEOUT_MS + 3000)).code, 4000);
});

test('an upgrade request carrying an Origin header is refused with HTTP 403', async (t) => {
  // WebSocket handshakes are exempt from the same-origin policy, so any page the operator
  // loads could otherwise reach a loopback-bound room.
  const { server, port } = await startServer();
  const client = connect(port, { origin: 'http://evil.example' });
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  const err = await client.failed;

  assert.ok(err instanceof Error);
  assert.match(err.message, /403/);
  assert.notEqual(client.ws.readyState, WebSocket.OPEN);
});

test('an upgrade request with no Origin header is admitted', async (t) => {
  // verifyClient reads ws's own version-aware info.origin rather than req.headers.origin
  // (which only exists for a hybi-13 handshake); a handshake that never sent Origin at
  // all — every non-browser client, including our own IXWebSocket client — must still be
  // let through, not refused for a header it never had reason to send.
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  const welcome = await handshake(client, 'nagare');

  assert.equal(welcome.type, 'welcome');
});

test('a connection beyond the pre-hello ceiling is closed 1008', async (t) => {
  // The ceiling is maxClients * 4 sockets that have not yet completed a hello.
  const { server, port } = await startServer({ maxClients: 1 });
  const silent = [];
  let extra = null;
  t.after(async () => {
    for (const client of silent) client.dispose();
    if (extra) extra.dispose();
    await server.close();
  });

  for (let i = 0; i < 4; i += 1) {
    const client = connect(port);
    silent.push(client);
    await open(client);
  }

  extra = connect(port);

  assert.equal((await expectClose(extra)).code, 1008);
});

// ---------------------------------------------------------------------------
// fan-out
// ---------------------------------------------------------------------------

test('a join is announced to the clients already in the room', async (t) => {
  const { server, port } = await startServer();
  const first = connect(port);
  const second = connect(port);
  t.after(async () => {
    first.dispose();
    second.dispose();
    await server.close();
  });

  await handshake(first, 'aki');
  const welcome = await handshake(second, 'nagare');
  const peerJoined = await nextOfType(first, 'peerJoined');

  assert.equal(peerJoined.clientId, welcome.clientId);
  assert.equal(peerJoined.name, 'nagare');
  assert.equal(peerJoined.role, 'observer');
});

test('an accepted delta reaches every client except its source', async (t) => {
  const { server, port } = await startServer();
  const controller = connect(port);
  const observerOne = connect(port);
  const observerTwo = connect(port);
  t.after(async () => {
    controller.dispose();
    observerOne.dispose();
    observerTwo.dispose();
    await server.close();
  });

  const controllerWelcome = await handshake(controller, 'aki');
  await handshake(observerOne, 'nagare');
  await handshake(observerTwo, 'kei');

  controller.send({ type: 'claimControl' });
  await nextOfType(controller, 'roleChanged');
  await nextOfType(observerOne, 'roleChanged');
  await nextOfType(observerTwo, 'roleChanged');

  controller.send({ type: 'delta', deck: 'A', changes: { gain: 0.5 } });

  for (const observer of [observerOne, observerTwo]) {
    const delta = await observer.next();
    assert.equal(delta.type, 'delta');
    assert.equal(delta.sourceClientId, controllerWelcome.clientId);
    assert.equal(delta.deck, 'A');
    assert.deepEqual(delta.changes, { gain: 0.5 });
    assert.ok(Number.isInteger(delta.serverSeq));
  }

  // The source must not have been sent its own delta: the next frame it sees is the
  // answer to a request made after the broadcast.
  controller.send({ type: 'requestSnapshot' });
  const next = await controller.next();
  assert.equal(next.type, 'snapshot');
  assert.equal(next.decks.A.gain, 0.5);
});

test('the server closes live connections with 1001 on close()', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  let stopped = false;
  t.after(async () => {
    client.dispose();
    if (!stopped) await server.close();
  });

  await handshake(client, 'nagare');
  await server.close();
  stopped = true;

  assert.equal((await expectClose(client)).code, 1001);
});

// ---------------------------------------------------------------------------
// framing and bad input
// ---------------------------------------------------------------------------

test('a frame over the 4096-byte limit closes 1009', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  client.sendRaw('x'.repeat(5000));

  assert.equal((await expectClose(client)).code, 1009);
});

test('a binary frame closes 1008', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  client.sendRaw(Buffer.from([0x01, 0x02, 0x03]), { binary: true });

  assert.equal((await expectClose(client)).code, 1008);
});

test('a frame that is not JSON draws a bad-message error and keeps the connection open', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  client.sendRaw('{not json');

  const error = await client.next();
  assert.equal(error.type, 'error');
  assert.equal(error.code, 'bad-message');
  assert.equal(error.message, 'malformed json');

  client.send({ type: 'requestSnapshot' });
  assert.equal((await client.next()).type, 'snapshot');
});

test('three consecutive invalid messages close 1008', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  client.sendRaw('{not json');
  client.sendRaw('{not json');
  client.sendRaw('{not json');

  assert.equal((await expectClose(client)).code, 1008);
});

test('a valid message between invalid ones resets the three-strike counter', async (t) => {
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  client.sendRaw('{not json');
  await nextOfType(client, 'error');
  client.sendRaw('{not json');
  await nextOfType(client, 'error');
  client.send({ type: 'requestSnapshot' });
  await nextOfType(client, 'snapshot');
  client.sendRaw('{not json');
  await nextOfType(client, 'error');

  client.send({ type: 'requestSnapshot' });
  assert.equal((await client.next()).type, 'snapshot');
  assert.equal(client.ws.readyState, WebSocket.OPEN);
});

test('three role rejections in a row do not close the connection', async (t) => {
  // Only bad-message-class failures count toward the three-strike close; an observer
  // clicking play is sending well-formed messages.
  const { server, port } = await startServer();
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');

  let rejections = 0;
  for (let i = 0; i < 3; i += 1) {
    client.send({ type: 'delta', deck: 'A', changes: { gain: 0.5 } });
    const error = await nextOfType(client, 'error');
    assert.equal(error.code, 'not-controller');
    rejections += 1;
  }

  assert.equal(rejections, 3);
  client.send({ type: 'requestSnapshot' });
  assert.equal((await nextOfType(client, 'snapshot')).type, 'snapshot');
  assert.equal(client.ws.readyState, WebSocket.OPEN);
});

// ---------------------------------------------------------------------------
// rate limiting
// ---------------------------------------------------------------------------

test('exhausting the token bucket draws a rate-limited error', async (t) => {
  const { server, port } = await startServer({
    rateLimit: { sustainedPerSecond: 1, burst: 3, banAfterMs: 5000 },
  });
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  for (let i = 0; i < 20; i += 1) client.send({ type: 'requestSnapshot' });
  await delay(300);

  const rateLimited = client.inbox.filter((frame) => frame.type === 'error' && frame.code === 'rate-limited');
  assert.ok(rateLimited.length >= 1, 'a rate-limited error should have been sent');
  assert.equal(rateLimited[0].message, 'too many messages');
});

test('a deficit episode draws exactly one rate-limited error, not one per dropped frame', async (t) => {
  const { server, port } = await startServer({
    rateLimit: { sustainedPerSecond: 1, burst: 3, banAfterMs: 5000 },
  });
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  for (let i = 0; i < 20; i += 1) client.send({ type: 'requestSnapshot' });
  await delay(300);

  const rateLimited = client.inbox.filter((frame) => frame.type === 'error' && frame.code === 'rate-limited');
  assert.equal(rateLimited.length, 1);
});

test('a continuous deficit past banAfterMs closes 4003', async (t) => {
  const { server, port } = await startServer({
    rateLimit: { sustainedPerSecond: 1, burst: 3, banAfterMs: 200 },
  });
  const client = connect(port);
  let flooding = true;
  t.after(async () => {
    flooding = false;
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare');
  const flood = (async () => {
    while (flooding && client.ws.readyState === WebSocket.OPEN) {
      client.send({ type: 'requestSnapshot' });
      await delay(10);
    }
  })();

  const closed = await expectClose(client, 5000);
  flooding = false;
  await flood;

  assert.equal(closed.code, 4003);
});

test('a client that returns to a conforming rate after a brief deficit is not banned, even once banAfterMs has elapsed since the deficit began', async (t) => {
  // rateLimitedSince clears the moment a frame is served (server.js:125-126), so the 5 s
  // (here, scaled down) window measures *continuous* violation, per 02-protocol.md:78 —
  // not merely "some deficit happened within the last banAfterMs of wall-clock time". A
  // client that overshoots once and then settles back under the sustained rate must not
  // be punished later for having been over it briefly, however long ago that was.
  const { server, port } = await startServer({
    rateLimit: { sustainedPerSecond: 10, burst: 2, banAfterMs: 150 },
  });
  const client = connect(port);
  t.after(async () => {
    client.dispose();
    await server.close();
  });

  await handshake(client, 'nagare'); // consumes one token of the burst-2 bucket

  // A burst well past the remaining single token: the first is served, the rest are
  // dropped, opening a deficit episode (one rate-limited error, per the test above).
  for (let i = 0; i < 5; i += 1) client.send({ type: 'requestSnapshot' });
  await nextOfType(client, 'error');

  // Long enough for the bucket to refill past 1 token (well under a second at 10/s) and
  // for banAfterMs to have fully elapsed since the deficit began — the case that would
  // trip a clock that only measured elapsed time rather than continuous violation.
  await delay(300);

  client.send({ type: 'requestSnapshot' });
  const reply = await client.next();

  assert.equal(reply.type, 'snapshot', 'the message should have been served, not dropped');
  assert.equal(client.ws.readyState, WebSocket.OPEN, 'serving a frame must clear the ban clock');
});
