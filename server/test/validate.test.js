// Unit tests for server/src/validate.js, derived from 02-protocol.md's Field reference
// and the validate.js contract in the server-core unit spec.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  validateHello,
  validateChanges,
  validateDelta,
  validateEnvelope,
} from '../src/validate.js';

const EXPECTED = { protocolVersion: 1, roomCode: 'demo-room' };

const hello = (overrides = {}) => ({
  type: 'hello',
  protocolVersion: 1,
  name: 'nagare',
  room: 'demo-room',
  ...overrides,
});

const NON_OBJECTS = [
  ['null', null],
  ['an array', []],
  ['a number', 7],
  ['a string', 'hello'],
  ['a boolean', true],
];

// ---------------------------------------------------------------------------
// validateHello
// ---------------------------------------------------------------------------

test('validateHello accepts a well-formed hello and returns the normalized value', () => {
  const result = validateHello(hello(), EXPECTED);
  assert.equal(result.ok, true);
  assert.deepEqual(result.value, { protocolVersion: 1, name: 'nagare', room: 'demo-room' });
});

test('validateHello reports a hello with no protocolVersion field as a version mismatch', () => {
  // Check order is asserted behaviour: 02-protocol.md maps "wrong/missing protocolVersion"
  // to close 4001, so an absent field must not be swallowed by the generic shape check.
  const msg = hello();
  delete msg.protocolVersion;
  const result = validateHello(msg, EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'protocol version mismatch');
});

for (const [label, version] of [
  ['a different integer', 2],
  ['zero', 0],
  ['a string', '1'],
  ['a float', 1.5],
  ['null', null],
  ['a boolean', true],
]) {
  test(`validateHello rejects a protocolVersion given as ${label} with a version mismatch`, () => {
    const result = validateHello(hello({ protocolVersion: version }), EXPECTED);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'protocol version mismatch');
  });
}

test('validateHello reports an absent protocolVersion ahead of any other defect in the hello', () => {
  const result = validateHello({ type: 'hello', name: '', room: 'demo-room' }, EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'protocol version mismatch');
});

for (const [label, msg] of NON_OBJECTS) {
  test(`validateHello rejects ${label} as a malformed hello`, () => {
    const result = validateHello(msg, EXPECTED);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'malformed hello');
  });
}

test('validateHello rejects a message whose type is not hello', () => {
  const result = validateHello(hello({ type: 'claimControl' }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello rejects a hello carrying an unknown extra field', () => {
  const result = validateHello(hello({ nickname: 'nagare' }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello rejects a hello with no name field', () => {
  const msg = hello();
  delete msg.name;
  const result = validateHello(msg, EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello rejects a hello with no room field', () => {
  const msg = hello();
  delete msg.room;
  const result = validateHello(msg, EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello accepts a name at the 32-character maximum', () => {
  const result = validateHello(hello({ name: 'a'.repeat(32) }), EXPECTED);
  assert.equal(result.ok, true);
});

test('validateHello accepts a single-character name', () => {
  const result = validateHello(hello({ name: 'a' }), EXPECTED);
  assert.equal(result.ok, true);
});

test('validateHello rejects a name one character over the maximum', () => {
  const result = validateHello(hello({ name: 'a'.repeat(33) }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello rejects an empty name', () => {
  const result = validateHello(hello({ name: '' }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello does not trim whitespace out of a name before measuring it', () => {
  // "length 1-32 with no trimming of any kind": a whitespace-only name is three printable
  // characters and must be accepted, not normalized away to an empty name.
  const result = validateHello(hello({ name: '   ' }), EXPECTED);
  assert.equal(result.ok, true);
  assert.equal(result.value.name, '   ');
});

for (const [label, codePoint] of [
  ['a C0 control character', 0x01],
  ['a NUL', 0x00],
  ['a newline', 0x0a],
  ['a tab', 0x09],
  ['a unit separator', 0x1f],
  ['a C1 control character', 0x85],
  ['a C1 boundary character', 0x9f],
]) {
  test(`validateHello rejects a name containing ${label}`, () => {
    const name = `na${String.fromCharCode(codePoint)}gare`;
    const result = validateHello(hello({ name }), EXPECTED);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'malformed hello');
  });
}

test('validateHello rejects a name containing an unpaired surrogate', () => {
  // An ill-formed name is relayed verbatim in peerJoined; a frame ws cannot encode as
  // UTF-8 would close an innocent peer instead of the joiner.
  const result = validateHello(hello({ name: `na${String.fromCharCode(0xd800)}gare` }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello accepts a name containing a well-formed surrogate pair', () => {
  const result = validateHello(hello({ name: 'nagare \u{1F3A7}' }), EXPECTED);
  assert.equal(result.ok, true);
});

for (const [label, codePoint] of [
  ['a right-to-left override', 0x202e],
  ['a left-to-right isolate', 0x2066],
  ['a pop directional isolate', 0x2069],
  ['a zero-width space', 0x200b],
  ['a zero-width joiner', 0x200d],
  ['a right-to-left mark', 0x200f],
]) {
  test(`validateHello rejects a name containing ${label}`, () => {
    // General-category Cf (Unicode "format" characters): invisible on their own, but a
    // bidi override relayed verbatim into every peer's list would let an observer's name
    // render as though it belonged to someone else, e.g. the controller.
    const name = `na${String.fromCodePoint(codePoint)}gare`;
    const result = validateHello(hello({ name }), EXPECTED);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'malformed hello');
  });
}

for (const [label, name] of [
  ['accented Latin letters', 'Amélie Núñez'],
  ['CJK characters', '中村七海'],
  ['an emoji', 'nagare 🎧'],
]) {
  test(`validateHello accepts an ordinary non-ASCII name: ${label}`, () => {
    // Cf rejection must not overreach into rejecting well-formed non-ASCII text that
    // carries no Cf code points of its own.
    const result = validateHello(hello({ name }), EXPECTED);
    assert.equal(result.ok, true);
    assert.equal(result.value.name, name);
  });
}

for (const [label, name] of [
  ['a number', 5],
  ['null', null],
  ['an object', {}],
]) {
  test(`validateHello rejects a name given as ${label}`, () => {
    const result = validateHello(hello({ name }), EXPECTED);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'malformed hello');
  });
}

test('validateHello reports a room code that does not match the server as a wrong room', () => {
  const result = validateHello(hello({ room: 'some-other-room' }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'wrong room');
});

test('validateHello accepts a room code at the 64-character maximum', () => {
  const roomCode = 'r'.repeat(64);
  const result = validateHello(hello({ room: roomCode }), { protocolVersion: 1, roomCode });
  assert.equal(result.ok, true);
  assert.equal(result.value.room, roomCode);
});

test('validateHello treats a room string over 64 characters as a shape failure, not a wrong room', () => {
  const result = validateHello(hello({ room: 'r'.repeat(65) }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello treats an empty room string as a shape failure, not a wrong room', () => {
  const result = validateHello(hello({ room: '' }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

test('validateHello treats a non-string room as a shape failure, not a wrong room', () => {
  const result = validateHello(hello({ room: 1234 }), EXPECTED);
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed hello');
});

// ---------------------------------------------------------------------------
// validateChanges - shape
// ---------------------------------------------------------------------------

for (const [label, changes] of NON_OBJECTS) {
  test(`validateChanges rejects changes given as ${label}`, () => {
    const result = validateChanges(changes);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'changes must be an object');
  });
}

test('validateChanges rejects an empty changes object', () => {
  const result = validateChanges({});
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'changes must not be empty');
});

test('validateChanges rejects an unknown field inside changes', () => {
  const result = validateChanges({ gain: 1.0, volume: 0.5 });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'unknown field in changes');
});

test('validateChanges rejects a field name that differs only in case', () => {
  const result = validateChanges({ TrackId: 'song-01' });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'unknown field in changes');
});

for (const key of ['__proto__', 'constructor', 'prototype']) {
  test(`validateChanges rejects ${key} as a key inside changes`, () => {
    // Built through JSON.parse so the key is a real own property, exactly as it arrives
    // off the wire, rather than an object-literal prototype assignment.
    const changes = JSON.parse(`{"gain": 1.0, ${JSON.stringify(key)}: {"polluted": true}}`);
    const result = validateChanges(changes);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'unknown field in changes');
  });
}

test('validateChanges leaves Object.prototype untouched when handed a __proto__ key', () => {
  const changes = JSON.parse('{"__proto__": {"polluted": true}}');
  validateChanges(changes);
  assert.equal({}.polluted, undefined);
});

test('validateChanges accepts every Field reference field in one changes object', () => {
  const changes = {
    trackId: 'song-01',
    playing: true,
    positionSeconds: 12.5,
    gain: 1.0,
    playbackRate: 1.0,
    pitchOffsetSemitones: 0,
    loop: { inSeconds: 4, outSeconds: 8 },
  };
  const result = validateChanges(changes);
  assert.equal(result.ok, true);
  assert.deepEqual(result.value, changes);
});

test('validateChanges returns a fresh object rather than the caller input', () => {
  const changes = { gain: 1.5 };
  const result = validateChanges(changes);
  assert.equal(result.ok, true);
  assert.notEqual(result.value, changes);
});

test('validateChanges returns a value that a later mutation of the input cannot reach', () => {
  const changes = { gain: 1.5 };
  const result = validateChanges(changes);
  changes.gain = 9;
  assert.equal(result.value.gain, 1.5);
});

// ---------------------------------------------------------------------------
// validateChanges - trackId
// ---------------------------------------------------------------------------

for (const [label, trackId] of [
  ['a plain id', 'song-01'],
  ['null', null],
  ['a single character', 'a'],
  ['a 64-character id', 'a'.repeat(64)],
  ['dots, underscores and hyphens', 'a.b_c-1'],
  ['a leading dot', '.hidden'],
]) {
  test(`validateChanges accepts a trackId that is ${label}`, () => {
    const result = validateChanges({ trackId });
    assert.equal(result.ok, true);
    assert.equal(result.value.trackId, trackId);
  });
}

for (const [label, trackId] of [
  ['empty', ''],
  ['65 characters long', 'a'.repeat(65)],
  ['a forward slash', 'dir/song'],
  ['a backslash', 'dir\\song'],
  ['a space', 'my song'],
  ['a tilde', 'song~1'],
  ['a number', 5],
  ['a boolean', true],
  ['an object', {}],
  ['an array', []],
]) {
  test(`validateChanges rejects a trackId that is ${label}`, () => {
    const result = validateChanges({ trackId });
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'invalid trackId');
  });
}

for (const [label, trackId] of [
  ['a bare dot', '.'],
  ['a bare double dot', '..'],
  ['an embedded double dot', 'a..b'],
  ['a leading double dot', '..song'],
  ['a trailing double dot', 'song..'],
]) {
  test(`validateChanges rejects a trackId that is ${label}, so it can never act as a path segment`, () => {
    const result = validateChanges({ trackId });
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'invalid trackId');
  });
}

// ---------------------------------------------------------------------------
// validateChanges - playing
// ---------------------------------------------------------------------------

test('validateChanges accepts playing:false on its own', () => {
  const result = validateChanges({ playing: false });
  assert.equal(result.ok, true);
  assert.equal(result.value.playing, false);
});

test('validateChanges accepts playing:true when positionSeconds travels with it', () => {
  const result = validateChanges({ playing: true, positionSeconds: 12 });
  assert.equal(result.ok, true);
});

test('validateChanges rejects playing:true without positionSeconds', () => {
  const result = validateChanges({ playing: true });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'playing:true requires positionSeconds');
});

test('validateChanges rejects playing:true alongside other fields but no positionSeconds', () => {
  const result = validateChanges({ playing: true, gain: 1.0 });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'playing:true requires positionSeconds');
});

for (const [label, playing] of [
  ['a string', 'true'],
  ['a number', 1],
  ['null', null],
  ['an object', {}],
]) {
  test(`validateChanges rejects playing given as ${label}`, () => {
    const result = validateChanges({ playing, positionSeconds: 0 });
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'playing must be a boolean');
  });
}

// ---------------------------------------------------------------------------
// validateChanges - numeric fields
// ---------------------------------------------------------------------------

const NUMERIC_FIELDS = [
  { field: 'positionSeconds', min: 0, max: 86400 },
  { field: 'gain', min: 0, max: 2 },
  { field: 'playbackRate', min: 0.5, max: 2 },
  { field: 'pitchOffsetSemitones', min: -12, max: 12 },
];

for (const { field, min, max } of NUMERIC_FIELDS) {
  test(`validateChanges accepts ${field} at its inclusive minimum ${min}`, () => {
    const result = validateChanges({ [field]: min });
    assert.equal(result.ok, true);
    assert.equal(result.value[field], min);
  });

  test(`validateChanges accepts ${field} at its inclusive maximum ${max}`, () => {
    const result = validateChanges({ [field]: max });
    assert.equal(result.ok, true);
    assert.equal(result.value[field], max);
  });

  test(`validateChanges accepts ${field} inside its range`, () => {
    const result = validateChanges({ [field]: (min + max) / 2 });
    assert.equal(result.ok, true);
  });

  test(`validateChanges rejects ${field} just below its minimum`, () => {
    const result = validateChanges({ [field]: min - 0.5 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.includes(field), `reason should name the field: ${result.reason}`);
    assert.match(result.reason, /out of range/);
  });

  test(`validateChanges rejects ${field} just above its maximum`, () => {
    const result = validateChanges({ [field]: max + 0.5 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.includes(field), `reason should name the field: ${result.reason}`);
    assert.match(result.reason, /out of range/);
  });

  for (const [label, value] of [
    ['NaN', Number.NaN],
    ['Infinity', Number.POSITIVE_INFINITY],
    ['-Infinity', Number.NEGATIVE_INFINITY],
    ['a numeric string', '1'],
    ['null', null],
    ['a boolean', true],
    ['an object', {}],
  ]) {
    test(`validateChanges rejects ${field} given as ${label}`, () => {
      const result = validateChanges({ [field]: value });
      assert.equal(result.ok, false);
      assert.ok(result.reason.includes(field), `reason should name the field: ${result.reason}`);
      assert.match(result.reason, /finite/);
    });
  }
}

test('validateChanges reports an out-of-range positionSeconds with the documented reason', () => {
  const result = validateChanges({ positionSeconds: 90000 });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'positionSeconds out of range');
});

test('validateChanges reports a non-finite positionSeconds with the documented reason', () => {
  const result = validateChanges({ positionSeconds: Number.NaN });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'positionSeconds must be a finite number');
});

test('validateChanges never echoes the offending value back in a reason', () => {
  const result = validateChanges({ gain: 9999.5 });
  assert.equal(result.ok, false);
  assert.equal(result.reason.includes('9999.5'), false);
});

// ---------------------------------------------------------------------------
// validateChanges - loop
// ---------------------------------------------------------------------------

test('validateChanges accepts a loop with in strictly before out', () => {
  const result = validateChanges({ loop: { inSeconds: 4, outSeconds: 8 } });
  assert.equal(result.ok, true);
  assert.deepEqual(result.value.loop, { inSeconds: 4, outSeconds: 8 });
});

test('validateChanges accepts a loop spanning the whole legal range', () => {
  const result = validateChanges({ loop: { inSeconds: 0, outSeconds: 86400 } });
  assert.equal(result.ok, true);
});

test('validateChanges accepts loop:null, which clears the loop', () => {
  const result = validateChanges({ loop: null });
  assert.equal(result.ok, true);
  assert.equal(result.value.loop, null);
});

for (const [label, loop] of [
  ['a zero-length loop', { inSeconds: 4, outSeconds: 4 }],
  ['a reversed loop', { inSeconds: 8, outSeconds: 4 }],
  ['a negative inSeconds', { inSeconds: -1, outSeconds: 4 }],
  ['an outSeconds beyond the maximum', { inSeconds: 0, outSeconds: 86401 }],
  ['a missing outSeconds', { inSeconds: 4 }],
  ['a missing inSeconds', { outSeconds: 8 }],
  ['an empty object', {}],
  ['an unknown extra field', { inSeconds: 4, outSeconds: 8, fadeSeconds: 1 }],
  ['a non-finite inSeconds', { inSeconds: Number.NaN, outSeconds: 8 }],
  ['an infinite outSeconds', { inSeconds: 0, outSeconds: Number.POSITIVE_INFINITY }],
  ['a string inSeconds', { inSeconds: '4', outSeconds: 8 }],
  ['a number', 4],
  ['a string', '4-8'],
  ['an array', [4, 8]],
  ['a boolean', true],
]) {
  test(`validateChanges rejects ${label} as a loop`, () => {
    const result = validateChanges({ loop });
    assert.equal(result.ok, false);
    assert.ok(
      result.reason.startsWith('loop'),
      `reason should name the loop field: ${result.reason}`,
    );
  });
}

test('validateChanges rejects a loop carrying a __proto__ key', () => {
  const changes = JSON.parse('{"loop": {"inSeconds": 0, "outSeconds": 8, "__proto__": {}}}');
  const result = validateChanges(changes);
  assert.equal(result.ok, false);
  assert.ok(result.reason.startsWith('loop'), `reason should name the loop field: ${result.reason}`);
});

// ---------------------------------------------------------------------------
// validateDelta
// ---------------------------------------------------------------------------

test('validateDelta accepts a well-formed delta and returns deck plus normalized changes', () => {
  const result = validateDelta({ type: 'delta', deck: 'A', changes: { gain: 1.5 } });
  assert.equal(result.ok, true);
  assert.deepEqual(result.value, { deck: 'A', changes: { gain: 1.5 } });
});

test('validateDelta accepts deck B', () => {
  const result = validateDelta({ type: 'delta', deck: 'B', changes: { playing: false } });
  assert.equal(result.ok, true);
  assert.equal(result.value.deck, 'B');
});

for (const [label, deck] of [
  ['C', 'C'],
  ['a lower-case a', 'a'],
  ['an empty string', ''],
  ['a number', 0],
  ['null', null],
]) {
  test(`validateDelta rejects a deck of ${label}`, () => {
    const result = validateDelta({ type: 'delta', deck, changes: { gain: 1 } });
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'invalid deck');
  });
}

test('validateDelta rejects a delta carrying an unknown envelope field', () => {
  const result = validateDelta({ type: 'delta', deck: 'A', changes: { gain: 1 }, serverSeq: 4 });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed delta');
});

test('validateDelta rejects a delta with no changes field', () => {
  const result = validateDelta({ type: 'delta', deck: 'A' });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed delta');
});

test('validateDelta rejects a delta with no deck field', () => {
  const result = validateDelta({ type: 'delta', changes: { gain: 1 } });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed delta');
});

test('validateDelta rejects a message whose type is not delta', () => {
  const result = validateDelta({ type: 'snapshot', deck: 'A', changes: { gain: 1 } });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'malformed delta');
});

for (const [label, msg] of NON_OBJECTS) {
  test(`validateDelta rejects ${label} as a malformed delta`, () => {
    const result = validateDelta(msg);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'malformed delta');
  });
}

test('validateDelta passes an empty-changes failure through unchanged', () => {
  const result = validateDelta({ type: 'delta', deck: 'A', changes: {} });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'changes must not be empty');
});

test('validateDelta passes an out-of-range changes failure through unchanged', () => {
  const result = validateDelta({ type: 'delta', deck: 'A', changes: { gain: 5 } });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'gain out of range');
});

// ---------------------------------------------------------------------------
// validateEnvelope
// ---------------------------------------------------------------------------

for (const [label, msg] of NON_OBJECTS) {
  test(`validateEnvelope rejects ${label} as not an object`, () => {
    const result = validateEnvelope(msg);
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'not an object');
  });
}

test('validateEnvelope rejects a message with no type field', () => {
  const result = validateEnvelope({ deck: 'A' });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'missing type');
});

test('validateEnvelope rejects a non-string type', () => {
  const result = validateEnvelope({ type: 7 });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'missing type');
});

test('validateEnvelope rejects a type outside the client-to-server whitelist', () => {
  const result = validateEnvelope({ type: 'shutdown' });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'unknown message type');
});

test('validateEnvelope rejects a server-to-client type sent by a client', () => {
  const result = validateEnvelope({ type: 'welcome' });
  assert.equal(result.ok, false);
  assert.equal(result.reason, 'unknown message type');
});

for (const type of ['claimControl', 'releaseControl', 'requestSnapshot']) {
  test(`validateEnvelope accepts a bare ${type}`, () => {
    const result = validateEnvelope({ type });
    assert.equal(result.ok, true);
    assert.deepEqual(result.value, { type });
  });

  test(`validateEnvelope rejects a ${type} carrying an extra field`, () => {
    const result = validateEnvelope({ type, deck: 'A' });
    assert.equal(result.ok, false);
    assert.equal(result.reason, 'unexpected fields');
  });
}

test('validateEnvelope accepts a hello envelope without judging its fields', () => {
  const result = validateEnvelope(hello());
  assert.equal(result.ok, true);
  assert.deepEqual(result.value, { type: 'hello' });
});

test('validateEnvelope accepts a delta envelope without judging its fields', () => {
  const result = validateEnvelope({ type: 'delta', deck: 'A', changes: { gain: 1 } });
  assert.equal(result.ok, true);
  assert.deepEqual(result.value, { type: 'delta' });
});
