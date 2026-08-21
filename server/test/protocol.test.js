// Unit tests for server/src/protocol.js. The file is a literal mirror of 02-protocol.md's
// tables, so every assertion here is a line from that document.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  PROTOCOL_VERSION,
  MAX_FRAME_BYTES,
  HELLO_TIMEOUT_MS,
  HEARTBEAT_INTERVAL_MS,
  MAX_CONSECUTIVE_INVALID,
  NAME_MAX_LENGTH,
  ROOM_CODE_MAX_LENGTH,
  DECK_IDS,
  TRACK_ID_PATTERN,
  DEFAULT_RATE_LIMIT,
  DEFAULT_MAX_CLIENTS,
  ERROR_CODES,
  CLOSE_CODES,
  FIELD_RANGES,
  DEFAULT_DECK_STATE,
} from '../src/protocol.js';

test('the protocol version is 2', () => {
  assert.equal(PROTOCOL_VERSION, 2);
});

test('the frame limit is 4096 bytes', () => {
  assert.equal(MAX_FRAME_BYTES, 4096);
});

test('the hello timeout is 5 seconds', () => {
  assert.equal(HELLO_TIMEOUT_MS, 5000);
});

test('the heartbeat interval is 15 seconds', () => {
  assert.equal(HEARTBEAT_INTERVAL_MS, 15000);
});

test('three consecutive invalid messages are the limit', () => {
  assert.equal(MAX_CONSECUTIVE_INVALID, 3);
});

test('a name may run to 32 characters', () => {
  assert.equal(NAME_MAX_LENGTH, 32);
});

test('a room code may run to 64 characters', () => {
  assert.equal(ROOM_CODE_MAX_LENGTH, 64);
});

test('the decks are A and B', () => {
  assert.deepEqual(DECK_IDS, ['A', 'B']);
});

test('the default room holds 8 clients', () => {
  assert.equal(DEFAULT_MAX_CLIENTS, 8);
});

test('the token bucket is 60 per second sustained, burst 120, banning after 5 seconds', () => {
  assert.deepEqual(DEFAULT_RATE_LIMIT, {
    sustainedPerSecond: 60,
    burst: 120,
    banAfterMs: 5000,
  });
});

test('the error codes mirror the protocol table', () => {
  assert.deepEqual(ERROR_CODES, {
    badMessage: 'bad-message',
    notController: 'not-controller',
    controlTaken: 'control-taken',
    rateLimited: 'rate-limited',
    unknownTrack: 'unknown-track',
  });
});

test('the close codes mirror the protocol table', () => {
  assert.deepEqual(CLOSE_CODES, {
    badHandshake: 4000,
    versionMismatch: 4001,
    roomFull: 4002,
    rateLimitBan: 4003,
    wrongRoom: 4004,
    policyViolation: 1008,
    oversizeFrame: 1009,
    goingAway: 1001,
    internalError: 1011,
  });
});

test('the field ranges mirror the Field reference', () => {
  assert.deepEqual(FIELD_RANGES, {
    positionSeconds: { min: 0, max: 86400 },
    gain: { min: 0, max: 2 },
    playbackRate: { min: 0.5, max: 2 },
    pitchOffsetSemitones: { min: -12, max: 12 },
    loopSeconds: { min: 0, max: 86400 },
  });
});

test('the default deck state is the Field reference defaults', () => {
  assert.deepEqual(DEFAULT_DECK_STATE, {
    trackId: null,
    playing: false,
    positionSeconds: 0,
    gain: 1.0,
    playbackRate: 1.0,
    pitchOffsetSemitones: 0,
    loop: null,
    repeat: true,
  });
});

test('the default deck state is frozen so no merge can mutate the shared default', () => {
  assert.equal(Object.isFrozen(DEFAULT_DECK_STATE), true);
  assert.throws(() => {
    DEFAULT_DECK_STATE.gain = 99;
  }, TypeError);
});

for (const trackId of ['a', 'song-01', 'a.b_c-1', 'a'.repeat(64)]) {
  test(`the trackId pattern matches ${JSON.stringify(trackId)}`, () => {
    assert.equal(TRACK_ID_PATTERN.test(trackId), true);
  });
}

for (const trackId of ['', 'a'.repeat(65), 'dir/song', 'dir\\song', 'my song', 'song~1']) {
  test(`the trackId pattern refuses ${JSON.stringify(trackId)}`, () => {
    assert.equal(TRACK_ID_PATTERN.test(trackId), false);
  });
}

test('the trackId pattern stays a literal mirror of the protocol regex and admits ..', () => {
  // The ".." exclusion is a separate check inside validate.js by design, so that this
  // constant can be diffed line by line against 02-protocol.md.
  assert.equal(TRACK_ID_PATTERN.test('a..b'), true);
});
