// White-box tests for room.js internal state that room.test.js's black-box suite does not
// reach: that the two snapshot-sending paths (the not-controller repair snapshot and
// requestSnapshot, room.js:139-183) are independent and unconditional — neither one gates
// or suppresses the other — and what happens when handleMessage is invoked again for a
// conn object handleDisconnect has already retired.

import test from 'node:test';
import assert from 'node:assert/strict';

import { createRoom } from '../src/room.js';

const ROOM_CODE = 'demo-room';

function fakeLogger() {
  const noop = () => {};
  return { error: noop, warn: noop, info: noop, debug: noop };
}

function fakeConn(remoteAddress = '10.0.0.7') {
  const sent = [];
  const closes = [];
  return {
    id: null,
    name: null,
    remoteAddress,
    send(obj) {
      sent.push(obj);
    },
    close(code, reason) {
      closes.push({ code, reason });
    },
    sent,
    closes,
  };
}

function makeRoom({ maxClients = 8, roomCode = ROOM_CODE } = {}) {
  return createRoom({ roomCode, maxClients, logger: fakeLogger(), protocolVersion: 1 });
}

function helloFrame(name = 'nagare', room = ROOM_CODE) {
  return { type: 'hello', protocolVersion: 1, name, room };
}

function join(room, { name = 'nagare' } = {}) {
  const conn = fakeConn();
  const result = room.handleMessage(conn, helloFrame(name));
  assert.equal(result.ok, true, 'setup join should succeed');
  return conn;
}

const drain = (conn) => conn.sent.splice(0);

const deltaFrame = (deck, changes) => ({ type: 'delta', deck, changes });

// ---------------------------------------------------------------------------
// requestSnapshot and the repair snapshot are independent, unconditional send paths
// (room.js:139-176) — neither one gates or suppresses the other.
// ---------------------------------------------------------------------------

test('an intervening requestSnapshot does not suppress the repair snapshot a later rejected delta draws', () => {
  // requestSnapshot (room.js:178-183) and the not-controller repair snapshot (room.js:161)
  // are two independent, unconditional sends with no shared state between them. A rejected
  // delta still draws its own error + snapshot even if the observer already asked for one
  // directly in between.
  const room = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(observer, { type: 'requestSnapshot' });
  drain(observer);

  const result = room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));

  assert.equal(result.errorCode, 'not-controller');
  assert.equal(observer.sent.length, 2);
  assert.equal(observer.sent[0].type, 'error');
  assert.equal(observer.sent[1].type, 'snapshot');
});

test('a rejected delta\'s repair snapshot does not suppress a later requestSnapshot: it is still answered', () => {
  const room = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(observer, deltaFrame('A', { gain: 0.5 })); // draws error + repair snapshot
  drain(observer);

  const result = room.handleMessage(observer, { type: 'requestSnapshot' });

  assert.equal(result.ok, true);
  assert.equal(observer.sent.length, 1);
  assert.equal(observer.sent[0].type, 'snapshot');
});

// ---------------------------------------------------------------------------
// handleMessage invoked again after handleDisconnect
// ---------------------------------------------------------------------------
//
// server.js's real wiring never calls handleMessage after handleDisconnect for the same
// conn, since ws never emits another 'message' event once 'close' has fired. These tests
// pin what room.js itself does if its public handleMessage/handleDisconnect API is used
// out of that order. handleClaimControl, handleReleaseControl and handleDelta each start
// with an `isMember` check (room.js:40, checked at 113, 128, 143) — the same membership
// test handleDisconnect itself performs (room.js:235) before mutating anything — so a
// conn a prior handleDisconnect has already retired can no longer claim control, release
// control, or apply a delta.

test('claimControl from a conn already retired by handleDisconnect is refused, not granted', () => {
  const room = makeRoom();
  const ghost = join(room, { name: 'aki' });
  room.handleDisconnect(ghost);
  assert.equal(room.clientCount(), 0, 'setup: the ghost should no longer be a registered client');

  const result = room.handleMessage(ghost, { type: 'claimControl' });

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'bad-message');
  assert.equal(room.getControllerId(), null);
});

test('releaseControl from a conn already retired by handleDisconnect is refused, not granted', () => {
  // Retiring the controller itself already frees control (handleDisconnect, room.js:239),
  // so this drives the ghost's own stale releaseControl in after a second, live client has
  // legitimately claimed it — the case isMember exists to stop the ghost from touching.
  const room = makeRoom();
  const ghost = join(room, { name: 'aki' });
  room.handleMessage(ghost, { type: 'claimControl' });
  room.handleDisconnect(ghost);
  const live = join(room, { name: 'nagare' });
  room.handleMessage(live, { type: 'claimControl' });
  assert.equal(room.getControllerId(), live.id, 'setup: the live client should hold control');

  const result = room.handleMessage(ghost, { type: 'releaseControl' });

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'bad-message');
  assert.equal(room.getControllerId(), live.id, 'the live controller must not be displaced by a ghost');
});

test('a delta from a conn already retired by handleDisconnect is refused, not applied', () => {
  const room = makeRoom();
  const ghost = join(room, { name: 'aki' });
  room.handleMessage(ghost, { type: 'claimControl' });
  room.handleDisconnect(ghost);
  const before = room.getSnapshot();

  const result = room.handleMessage(ghost, deltaFrame('A', { gain: 0.5 }));

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'bad-message');
  assert.deepEqual(room.getSnapshot(), before, 'a ghost delta must not mutate canonical state');
});

test('a disconnect leaves the departed id fully free: a fresh client can reuse the slot and controller role', () => {
  // Sanity check alongside the finding above: the *normal* path (a fresh conn, not the
  // retired one) behaves correctly after a controller disconnects.
  const room = makeRoom({ maxClients: 1 });
  const first = join(room, { name: 'aki' });
  room.handleMessage(first, { type: 'claimControl' });
  room.handleDisconnect(first);

  const second = fakeConn();
  const result = room.handleMessage(second, helloFrame('nagare'));
  assert.equal(result.ok, true);

  const claim = room.handleMessage(second, { type: 'claimControl' });
  assert.equal(claim.ok, true);
  assert.equal(room.getControllerId(), second.id);
});
