// Unit tests for server/src/room.js, driven with fake connections and no sockets at all,
// per 05-testing.md ("room.js via fake clients"). Every constant here is spelled out from
// 02-protocol.md rather than imported from protocol.js, so these tests pin the protocol
// document rather than the server's own copy of it.

import test from 'node:test';
import assert from 'node:assert/strict';

import { createRoom } from '../src/room.js';

const ROOM_CODE = 'demo-room';

const DECK_DEFAULTS = {
  trackId: null,
  playing: false,
  positionSeconds: 0,
  gain: 1.0,
  playbackRate: 1.0,
  pitchOffsetSemitones: 0,
  loop: null,
};

const CLIENT_ID_PATTERN = /^c-[0-9a-f]{4}$/;

function fakeLogger() {
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
  const logger = fakeLogger();
  const room = createRoom({ roomCode, maxClients, logger, protocolVersion: 1 });
  return { room, logger };
}

function helloFrame(name = 'nagare', room = ROOM_CODE) {
  return { type: 'hello', protocolVersion: 1, name, room };
}

// Joins a fresh connection and returns it once the welcome has been consumed.
function join(room, { name = 'nagare', remoteAddress = '10.0.0.7' } = {}) {
  const conn = fakeConn(remoteAddress);
  const result = room.handleMessage(conn, helloFrame(name));
  assert.equal(result.ok, true, 'setup join should succeed');
  return conn;
}

const drain = (conn) => conn.sent.splice(0);

const framesOfType = (conn, type) => conn.sent.filter((frame) => frame.type === type);

const deltaFrame = (deck, changes) => ({ type: 'delta', deck, changes });

// ---------------------------------------------------------------------------
// hello / welcome
// ---------------------------------------------------------------------------

test('a valid hello draws a welcome to the joiner alone', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  const result = room.handleMessage(conn, helloFrame());

  assert.equal(result.ok, true);
  assert.equal(conn.sent.length, 1);
  assert.equal(conn.sent[0].type, 'welcome');
});

test('the welcome carries the assigned client id in the c-<4 hex> form', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame());

  assert.match(conn.sent[0].clientId, CLIENT_ID_PATTERN);
});

test('the room writes the assigned id and name onto the connection', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame('aki'));

  assert.equal(conn.id, conn.sent[0].clientId);
  assert.equal(conn.name, 'aki');
});

test('a joiner is welcomed as an observer', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame());

  assert.equal(conn.sent[0].role, 'observer');
});

test('the welcome snapshot holds deck A at the Field reference defaults', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame());

  assert.deepEqual(conn.sent[0].snapshot, { decks: { A: DECK_DEFAULTS } });
});

test('the welcome carries an integer serverSeq matching the room snapshot', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame());

  assert.ok(Number.isInteger(conn.sent[0].serverSeq), 'serverSeq should be an integer');
  assert.equal(conn.sent[0].serverSeq, room.getSnapshot().serverSeq);
});

test('the first joiner is welcomed into an empty peers list', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame());

  assert.deepEqual(conn.sent[0].peers, []);
});

test('the welcome peers list names every other client and omits the joiner', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  room.handleMessage(first, { type: 'claimControl' });

  const second = fakeConn();
  room.handleMessage(second, helloFrame('nagare'));

  assert.deepEqual(second.sent[0].peers, [
    { clientId: first.id, name: 'aki', role: 'controller' },
  ]);
});

test('a join is announced to the existing clients only', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  drain(first);

  const second = fakeConn();
  room.handleMessage(second, helloFrame('nagare'));

  assert.deepEqual(first.sent, [
    { type: 'peerJoined', clientId: second.id, name: 'nagare', role: 'observer' },
  ]);
  assert.deepEqual(framesOfType(second, 'peerJoined'), []);
});

test('every client is given a distinct id', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });

  assert.notEqual(first.id, second.id);
});

test('clientCount counts the registered clients', () => {
  const { room } = makeRoom();
  assert.equal(room.clientCount(), 0);

  join(room, { name: 'aki' });
  assert.equal(room.clientCount(), 1);

  join(room, { name: 'nagare' });
  assert.equal(room.clientCount(), 2);
});

// ---------------------------------------------------------------------------
// handshake failures
// ---------------------------------------------------------------------------

test('a hello declaring an unspoken protocol version closes 4001', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  const result = room.handleMessage(conn, { ...helloFrame(), protocolVersion: 99 });

  assert.deepEqual(conn.closes.map((c) => c.code), [4001]);
  assert.equal(result.ok, false);
  assert.equal(result.errorCode, undefined);
});

test('a hello with no protocolVersion field closes 4001, not 4000', () => {
  const { room } = makeRoom();
  const conn = fakeConn();
  const msg = helloFrame();
  delete msg.protocolVersion;

  room.handleMessage(conn, msg);

  assert.deepEqual(conn.closes.map((c) => c.code), [4001]);
});

test('a hello quoting the wrong room code closes 4004', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame('nagare', 'some-other-room'));

  assert.deepEqual(conn.closes.map((c) => c.code), [4004]);
});

test('a structurally malformed hello closes 4000', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, { ...helloFrame(), name: '' });

  assert.deepEqual(conn.closes.map((c) => c.code), [4000]);
});

test('a failed handshake sends no frame before closing', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame('nagare', 'some-other-room'));

  assert.deepEqual(conn.sent, []);
});

test('a failed handshake registers no client', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame('nagare', 'some-other-room'));

  assert.equal(room.clientCount(), 0);
  assert.equal(conn.id, null);
});

test('a hello arriving at a full room closes 4002', () => {
  const { room } = makeRoom({ maxClients: 1 });
  join(room, { name: 'aki' });

  const conn = fakeConn();
  const result = room.handleMessage(conn, helloFrame('nagare'));

  assert.deepEqual(conn.closes.map((c) => c.code), [4002]);
  assert.equal(result.ok, false);
  assert.equal(result.errorCode, undefined);
});

test('a bad hello to a full room closes for its own reason, not room-full', () => {
  // The capacity check runs after validation so that a 4002 can never confirm a guessed
  // room code.
  const { room } = makeRoom({ maxClients: 1 });
  join(room, { name: 'aki' });

  const conn = fakeConn();
  room.handleMessage(conn, { ...helloFrame(), protocolVersion: 99 });

  assert.deepEqual(conn.closes.map((c) => c.code), [4001]);
});

test('a handshake failure is logged at info with the close code and remote address', () => {
  const { room, logger } = makeRoom();
  const conn = fakeConn('192.168.1.44');

  room.handleMessage(conn, helloFrame('nagare', 'some-other-room'));

  const line = logger.lines.find((entry) => entry.event === 'handshake-failed');
  assert.ok(line, 'a handshake-failed line should be logged');
  assert.equal(line.level, 'info');
  assert.equal(line.fields.closeCode, 4004);
  assert.equal(line.fields.remoteAddress, '192.168.1.44');
});

test('a handshake failure log never carries the room code or the submitted guess', () => {
  const { room, logger } = makeRoom();
  const conn = fakeConn();

  room.handleMessage(conn, helloFrame('nagare', 'guessed-room'));

  const serialized = JSON.stringify(logger.lines);
  assert.equal(serialized.includes(ROOM_CODE), false, 'room code must never be logged');
  assert.equal(serialized.includes('guessed-room'), false, 'a guessed room must not be echoed');
});

test('a successful join is logged at info with the client id, name and remote address', () => {
  const { room, logger } = makeRoom();
  const conn = fakeConn('192.168.1.9');

  room.handleMessage(conn, helloFrame('aki'));

  const line = logger.lines.find((entry) => entry.event === 'client-joined');
  assert.ok(line, 'a client-joined line should be logged');
  assert.equal(line.level, 'info');
  assert.equal(line.fields.clientId, conn.id);
  assert.equal(line.fields.name, 'aki');
  assert.equal(line.fields.remoteAddress, '192.168.1.9');
});

test('a join log never carries the room code', () => {
  const { room, logger } = makeRoom();
  join(room, { name: 'aki' });

  assert.equal(JSON.stringify(logger.lines).includes(ROOM_CODE), false);
});

// ---------------------------------------------------------------------------
// dispatch guards
// ---------------------------------------------------------------------------

test('a null frame from a registered client draws a bad-message error rather than throwing', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  const result = room.handleMessage(conn, null);

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'bad-message');
  assert.equal(conn.sent.length, 1);
  assert.equal(conn.sent[0].type, 'error');
  assert.equal(conn.sent[0].code, 'bad-message');
  assert.equal(conn.sent[0].message, 'not an object');
});

test('a null frame before the handshake closes 4000 rather than throwing', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  const result = room.handleMessage(conn, null);

  assert.deepEqual(conn.closes.map((c) => c.code), [4000]);
  assert.equal(result.ok, false);
  assert.equal(result.errorCode, undefined);
});

for (const [label, frame] of [
  ['an array', []],
  ['a number', 42],
  ['a string', 'hello'],
  ['a boolean', true],
]) {
  test(`${label} from a registered client draws a bad-message error`, () => {
    const { room } = makeRoom();
    const conn = join(room);
    drain(conn);

    const result = room.handleMessage(conn, frame);

    assert.equal(result.errorCode, 'bad-message');
    assert.equal(conn.sent[0].type, 'error');
    assert.deepEqual(conn.closes, []);
  });
}

test('a well-formed claimControl sent before any hello closes 4000', () => {
  const { room } = makeRoom();
  const conn = fakeConn();

  const result = room.handleMessage(conn, { type: 'claimControl' });

  assert.deepEqual(conn.closes.map((c) => c.code), [4000]);
  assert.equal(result.ok, false);
  assert.equal(result.errorCode, undefined);
});

test('a second hello on a registered connection is a bad-message, not a re-registration', () => {
  const { room } = makeRoom();
  const conn = join(room, { name: 'aki' });
  const originalId = conn.id;
  drain(conn);

  const result = room.handleMessage(conn, helloFrame('nagare'));

  assert.equal(result.errorCode, 'bad-message');
  assert.equal(conn.sent[0].type, 'error');
  assert.equal(conn.id, originalId);
  assert.equal(room.clientCount(), 1);
});

test('an unknown message type draws a bad-message error', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  const result = room.handleMessage(conn, { type: 'shutdown' });

  assert.equal(result.errorCode, 'bad-message');
  assert.equal(conn.sent[0].code, 'bad-message');
});

test('a claimControl carrying an extra field draws a bad-message error', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  const result = room.handleMessage(conn, { type: 'claimControl', deck: 'A' });

  assert.equal(result.errorCode, 'bad-message');
  assert.equal(room.getControllerId(), null);
});

// ---------------------------------------------------------------------------
// control
// ---------------------------------------------------------------------------

test('an unclaimed claimControl makes the sender the controller', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  const result = room.handleMessage(conn, { type: 'claimControl' });

  assert.equal(result.ok, true);
  assert.equal(room.getControllerId(), conn.id);
});

test('a claim broadcasts roleChanged to everyone including the new controller', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });
  drain(first);
  drain(second);

  room.handleMessage(first, { type: 'claimControl' });

  const expected = { type: 'roleChanged', clientId: first.id, role: 'controller' };
  assert.deepEqual(first.sent, [expected]);
  assert.deepEqual(second.sent, [expected]);
});

test('the room does not store a role on the connection', () => {
  // Role is derived from the room's controllerId; a second copy would be an unenforced
  // two-writer invariant.
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  assert.equal(Object.hasOwn(conn, 'role'), false);
});

test('a claim from a second client draws control-taken and leaves control where it is', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });
  room.handleMessage(first, { type: 'claimControl' });
  drain(first);
  drain(second);

  const result = room.handleMessage(second, { type: 'claimControl' });

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'control-taken');
  assert.equal(second.sent.length, 1);
  assert.equal(second.sent[0].type, 'error');
  assert.equal(second.sent[0].code, 'control-taken');
  assert.deepEqual(first.sent, [], 'a refused claim is not broadcast');
  assert.equal(room.getControllerId(), first.id);
});

test('a re-claim from the current controller draws control-taken', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  drain(conn);

  const result = room.handleMessage(conn, { type: 'claimControl' });

  assert.equal(result.errorCode, 'control-taken');
  assert.equal(conn.sent[0].code, 'control-taken');
  assert.equal(room.getControllerId(), conn.id);
});

test('a release from the controller frees control', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  drain(conn);

  const result = room.handleMessage(conn, { type: 'releaseControl' });

  assert.equal(result.ok, true);
  assert.equal(room.getControllerId(), null);
});

test('a release broadcasts roleChanged to observer to everyone including the subject', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });
  room.handleMessage(first, { type: 'claimControl' });
  drain(first);
  drain(second);

  room.handleMessage(first, { type: 'releaseControl' });

  const expected = { type: 'roleChanged', clientId: first.id, role: 'observer' };
  assert.deepEqual(first.sent, [expected]);
  assert.deepEqual(second.sent, [expected]);
});

test('a release from a non-controller draws not-controller and changes nothing', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });
  room.handleMessage(first, { type: 'claimControl' });
  drain(first);
  drain(second);

  const result = room.handleMessage(second, { type: 'releaseControl' });

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'not-controller');
  assert.equal(second.sent.length, 1);
  assert.equal(second.sent[0].code, 'not-controller');
  assert.deepEqual(first.sent, []);
  assert.equal(room.getControllerId(), first.id);
});

test('a release with nobody holding control draws not-controller', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  const result = room.handleMessage(conn, { type: 'releaseControl' });

  assert.equal(result.errorCode, 'not-controller');
});

// ---------------------------------------------------------------------------
// delta
// ---------------------------------------------------------------------------

test('an accepted delta merges only the fields it carries into the deck', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  const result = room.handleMessage(conn, deltaFrame('A', { gain: 0.5 }));

  assert.equal(result.ok, true);
  assert.deepEqual(room.getSnapshot().decks.A, { ...DECK_DEFAULTS, gain: 0.5 });
});

test('an accepted delta increments serverSeq by exactly one', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  const before = room.getSnapshot().serverSeq;

  room.handleMessage(conn, deltaFrame('A', { gain: 0.5 }));

  assert.equal(room.getSnapshot().serverSeq, before + 1);
});

test('an accepted delta is broadcast to every client except its source', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observerOne = join(room, { name: 'nagare' });
  const observerTwo = join(room, { name: 'kei' });
  room.handleMessage(controller, { type: 'claimControl' });
  drain(controller);
  drain(observerOne);
  drain(observerTwo);

  room.handleMessage(controller, deltaFrame('A', { gain: 0.5 }));

  const expected = {
    type: 'delta',
    serverSeq: room.getSnapshot().serverSeq,
    sourceClientId: controller.id,
    deck: 'A',
    changes: { gain: 0.5 },
  };
  assert.deepEqual(observerOne.sent, [expected]);
  assert.deepEqual(observerTwo.sent, [expected]);
  assert.deepEqual(controller.sent, [], 'the source already applied the delta optimistically');
});

test('a claim does not move serverSeq', () => {
  const { room } = makeRoom();
  const conn = join(room);
  const before = room.getSnapshot().serverSeq;

  room.handleMessage(conn, { type: 'claimControl' });

  assert.equal(room.getSnapshot().serverSeq, before);
});

test('deck B is absent from the snapshot until a delta touches it', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  assert.equal(Object.hasOwn(room.getSnapshot().decks, 'B'), false);
});

test('a delta targeting deck B creates it from the Field reference defaults', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  room.handleMessage(conn, deltaFrame('B', { playbackRate: 1.5 }));

  assert.deepEqual(room.getSnapshot().decks.B, { ...DECK_DEFAULTS, playbackRate: 1.5 });
});

test('a delta on deck B leaves deck A untouched', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  room.handleMessage(conn, deltaFrame('B', { playbackRate: 1.5 }));

  assert.deepEqual(room.getSnapshot().decks.A, DECK_DEFAULTS);
});

test('a delta carrying loop:null clears a previously set loop', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  room.handleMessage(conn, deltaFrame('A', { loop: { inSeconds: 4, outSeconds: 8 } }));
  assert.deepEqual(room.getSnapshot().decks.A.loop, { inSeconds: 4, outSeconds: 8 });

  room.handleMessage(conn, deltaFrame('A', { loop: null }));

  assert.equal(room.getSnapshot().decks.A.loop, null);
});

test('a delta carrying trackId:null clears the track', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  room.handleMessage(conn, deltaFrame('A', { trackId: 'song-01' }));

  room.handleMessage(conn, deltaFrame('A', { trackId: null }));

  assert.equal(room.getSnapshot().decks.A.trackId, null);
});

test('an invalid delta from the controller draws bad-message and mutates nothing', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });
  const before = room.getSnapshot();
  drain(conn);

  const result = room.handleMessage(conn, deltaFrame('A', { gain: 5 }));

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'bad-message');
  assert.equal(conn.sent[0].type, 'error');
  assert.equal(conn.sent[0].code, 'bad-message');
  assert.deepEqual(room.getSnapshot(), before);
});

test('a delta is rejected whole, with no partial application of its valid fields', () => {
  const { room } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  room.handleMessage(conn, deltaFrame('A', { gain: 0.5, playbackRate: 9 }));

  assert.deepEqual(room.getSnapshot().decks.A, DECK_DEFAULTS);
});

test('an accepted delta is logged at debug with field names but never values', () => {
  const { room, logger } = makeRoom();
  const conn = join(room);
  room.handleMessage(conn, { type: 'claimControl' });

  room.handleMessage(conn, deltaFrame('A', { gain: 0.5 }));

  const line = logger.lines.find((entry) => entry.event === 'delta-applied');
  assert.ok(line, 'a delta-applied line should be logged');
  assert.equal(line.level, 'debug');
  assert.deepEqual(line.fields, { deck: 'A', fields: ['gain'] });
});

// ---------------------------------------------------------------------------
// delta from a non-controller
// ---------------------------------------------------------------------------

test('a delta from a non-controller draws an error then a repair snapshot, to the sender alone', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  drain(controller);
  drain(observer);

  const result = room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));

  assert.equal(result.ok, false);
  assert.equal(result.errorCode, 'not-controller');
  assert.equal(observer.sent.length, 2);
  assert.equal(observer.sent[0].type, 'error');
  assert.equal(observer.sent[0].code, 'not-controller');
  assert.equal(observer.sent[1].type, 'snapshot');
  assert.deepEqual(controller.sent, [], 'a refused delta is never broadcast');
});

test('a delta from a non-controller mutates neither the decks nor serverSeq', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  const before = room.getSnapshot();

  room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));

  assert.deepEqual(room.getSnapshot(), before);
});

test('the repair snapshot carries the canonical serverSeq and decks', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(controller, deltaFrame('A', { gain: 0.25 }));
  drain(observer);

  room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));

  const snapshot = observer.sent[1];
  const canonical = room.getSnapshot();
  assert.equal(snapshot.serverSeq, canonical.serverSeq);
  assert.deepEqual(snapshot.decks, canonical.decks);
});

test('a second refused delta against unchanged state draws the error and a second snapshot', () => {
  // Per 02-protocol.md:41, every refused delta draws error{not-controller} followed by a
  // snapshot — not just the first at a given serverSeq. What needs repairing is the
  // sender's own optimistic state, which moves with each delta it applies locally even
  // though serverSeq (by definition) has not moved, since nothing was accepted. A client
  // with two deltas in flight after losing control must be repaired for both, not just
  // the first, or it is left permanently wrong for the second.
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));
  drain(observer);

  room.handleMessage(observer, deltaFrame('A', { gain: 0.6 }));

  assert.equal(observer.sent.length, 2);
  assert.equal(observer.sent[0].type, 'error');
  assert.equal(observer.sent[0].code, 'not-controller');
  assert.equal(observer.sent[1].type, 'snapshot');
});

test('a delta rejected after an intervening accepted delta still draws its own repair snapshot', () => {
  // Every rejected delta draws its own error + snapshot unconditionally (room.js:154-163);
  // there is no per-connection dedup state to disarm or re-arm. This case pins that an
  // accepted delta from the controller landing in between two of the observer's rejected
  // deltas changes nothing about the second rejection's own error + snapshot response.
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(observer, deltaFrame('A', { gain: 0.5 }));
  room.handleMessage(controller, deltaFrame('A', { gain: 0.25 }));
  drain(observer);

  room.handleMessage(observer, deltaFrame('A', { gain: 0.6 }));

  assert.equal(observer.sent.length, 2);
  assert.equal(observer.sent[1].type, 'snapshot');
});

test('a rejected delta draws its own repair snapshot regardless of another connection having just drawn one', () => {
  // Unconditional per rejection, not deduplicated by any shared or per-connection state
  // (room.js:154-163): a second connection's rejected delta is unaffected by a first
  // connection's rejected delta having just been answered. With the dedup this test was
  // originally named for now gone, this coincides with the single-connection case above
  // ('a delta from a non-controller draws an error then a repair snapshot...'); kept as an
  // explicit multi-connection sanity check rather than removed.
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const first = join(room, { name: 'nagare' });
  const second = join(room, { name: 'kei' });
  room.handleMessage(controller, { type: 'claimControl' });
  room.handleMessage(first, deltaFrame('A', { gain: 0.5 }));
  drain(second);

  room.handleMessage(second, deltaFrame('A', { gain: 0.5 }));

  assert.equal(second.sent.length, 2);
  assert.equal(second.sent[1].type, 'snapshot');
});

test('a delta from a non-controller is validated before its role is checked', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const observer = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  drain(observer);

  const result = room.handleMessage(observer, deltaFrame('A', { gain: 5 }));

  assert.equal(result.errorCode, 'bad-message');
  assert.equal(observer.sent[0].code, 'bad-message');
});

// ---------------------------------------------------------------------------
// requestSnapshot / getSnapshot
// ---------------------------------------------------------------------------

test('requestSnapshot answers the sender alone with the canonical state', () => {
  const { room } = makeRoom();
  const first = join(room, { name: 'aki' });
  const second = join(room, { name: 'nagare' });
  drain(first);
  drain(second);

  const result = room.handleMessage(first, { type: 'requestSnapshot' });

  assert.equal(result.ok, true);
  assert.equal(first.sent.length, 1);
  assert.equal(first.sent[0].type, 'snapshot');
  assert.deepEqual(first.sent[0].decks, room.getSnapshot().decks);
  assert.equal(first.sent[0].serverSeq, room.getSnapshot().serverSeq);
  assert.deepEqual(second.sent, []);
});

test('requestSnapshot does not move serverSeq', () => {
  const { room } = makeRoom();
  const conn = join(room);
  const before = room.getSnapshot().serverSeq;

  room.handleMessage(conn, { type: 'requestSnapshot' });

  assert.equal(room.getSnapshot().serverSeq, before);
});

test('a repeated requestSnapshot is answered every time, without deduplication', () => {
  const { room } = makeRoom();
  const conn = join(room);
  drain(conn);

  room.handleMessage(conn, { type: 'requestSnapshot' });
  room.handleMessage(conn, { type: 'requestSnapshot' });

  assert.equal(framesOfType(conn, 'snapshot').length, 2);
});

test('getSnapshot hands back a deep copy that cannot reach into room state', () => {
  const { room } = makeRoom();
  const snapshot = room.getSnapshot();

  snapshot.decks.A.gain = 99;
  snapshot.serverSeq = 999;

  assert.equal(room.getSnapshot().decks.A.gain, 1.0);
  assert.notEqual(room.getSnapshot().serverSeq, 999);
});

test('two getSnapshot calls do not share deck objects', () => {
  const { room } = makeRoom();

  assert.notEqual(room.getSnapshot().decks.A, room.getSnapshot().decks.A);
});

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------

test('a disconnect announces peerLeft to the remaining clients', () => {
  const { room } = makeRoom();
  const leaving = join(room, { name: 'aki' });
  const staying = join(room, { name: 'nagare' });
  drain(staying);

  room.handleDisconnect(leaving);

  assert.deepEqual(staying.sent, [{ type: 'peerLeft', clientId: leaving.id }]);
});

test('a disconnect drops the client from the room count', () => {
  const { room } = makeRoom();
  const leaving = join(room, { name: 'aki' });
  join(room, { name: 'nagare' });

  room.handleDisconnect(leaving);

  assert.equal(room.clientCount(), 1);
});

test('a controller disconnect frees control', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });

  room.handleDisconnect(controller);

  assert.equal(room.getControllerId(), null);
});

test('a controller disconnect broadcasts peerLeft and then roleChanged, in that order', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const staying = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  drain(staying);

  room.handleDisconnect(controller);

  assert.deepEqual(staying.sent, [
    { type: 'peerLeft', clientId: controller.id },
    { type: 'roleChanged', clientId: controller.id, role: 'observer' },
  ]);
});

test('a controller disconnect promotes nobody', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const staying = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });

  room.handleDisconnect(controller);

  assert.equal(room.getControllerId(), null);
  assert.equal(
    staying.sent.some((frame) => frame.type === 'roleChanged' && frame.clientId === staying.id),
    false,
  );
});

test('an observer disconnect broadcasts peerLeft without a roleChanged', () => {
  const { room } = makeRoom();
  const controller = join(room, { name: 'aki' });
  const leaving = join(room, { name: 'nagare' });
  room.handleMessage(controller, { type: 'claimControl' });
  drain(controller);

  room.handleDisconnect(leaving);

  assert.deepEqual(controller.sent, [{ type: 'peerLeft', clientId: leaving.id }]);
  assert.equal(room.getControllerId(), controller.id);
});

test('a repeated disconnect announces the departure only once', () => {
  const { room } = makeRoom();
  const leaving = join(room, { name: 'aki' });
  const staying = join(room, { name: 'nagare' });
  room.handleMessage(leaving, { type: 'claimControl' });
  drain(staying);

  room.handleDisconnect(leaving);
  room.handleDisconnect(leaving);

  assert.equal(framesOfType(staying, 'peerLeft').length, 1);
  assert.equal(framesOfType(staying, 'roleChanged').length, 1);
});

test('a disconnect for a connection that never said hello is a no-op', () => {
  const { room } = makeRoom();
  const staying = join(room, { name: 'aki' });
  drain(staying);
  const never = fakeConn();

  room.handleDisconnect(never);

  assert.deepEqual(staying.sent, []);
  assert.deepEqual(never.sent, []);
  assert.equal(room.clientCount(), 1);
});

test('a departure is logged at info with the client id', () => {
  const { room, logger } = makeRoom();
  const leaving = join(room, { name: 'aki' });

  room.handleDisconnect(leaving);

  const line = logger.lines.find((entry) => entry.event === 'client-left');
  assert.ok(line, 'a client-left line should be logged');
  assert.equal(line.level, 'info');
  assert.equal(line.fields.clientId, leaving.id);
});

test('a departed client frees its slot in a full room', () => {
  const { room } = makeRoom({ maxClients: 1 });
  const first = join(room, { name: 'aki' });
  room.handleDisconnect(first);

  const second = fakeConn();
  const result = room.handleMessage(second, helloFrame('nagare'));

  assert.equal(result.ok, true);
  assert.match(second.id, CLIENT_ID_PATTERN);
});
