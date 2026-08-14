// Wire-protocol constants for the sync server. Every value here is a literal mirror of a
// table in docs/plan/02-protocol.md and must remain checkable against it line by line —
// nothing that is not part of the wire contract belongs in this file.

export const PROTOCOL_VERSION = 1;

export const MAX_FRAME_BYTES = 4096;

export const HELLO_TIMEOUT_MS = 5000;

export const HEARTBEAT_INTERVAL_MS = 15000;

export const MAX_CONSECUTIVE_INVALID = 3;

export const NAME_MAX_LENGTH = 32;

export const ROOM_CODE_MAX_LENGTH = 64;

export const DECK_IDS = ['A', 'B'];

// Exactly the protocol table's regex. The extra rejection of "." / ".." / embedded ".."
// lives in validate.js so this constant stays a literal mirror of the table.
export const TRACK_ID_PATTERN = /^[A-Za-z0-9._-]{1,64}$/;

export const DEFAULT_RATE_LIMIT = { sustainedPerSecond: 60, burst: 120, banAfterMs: 5000 };

export const DEFAULT_MAX_CLIENTS = 8;

export const ERROR_CODES = {
  badMessage: 'bad-message',
  notController: 'not-controller',
  controlTaken: 'control-taken',
  rateLimited: 'rate-limited',
  unknownTrack: 'unknown-track', // reserved by 02-protocol.md; nothing emits it yet
};

export const CLOSE_CODES = {
  badHandshake: 4000,
  versionMismatch: 4001,
  roomFull: 4002,
  rateLimitBan: 4003,
  wrongRoom: 4004,
  policyViolation: 1008,
  oversizeFrame: 1009,
  goingAway: 1001, // graceful shutdown
  internalError: 1011, // a handler threw (06-security.md §Server rules 5)
};

export const FIELD_RANGES = {
  positionSeconds: { min: 0, max: 86400 },
  gain: { min: 0, max: 2 },
  playbackRate: { min: 0.5, max: 2 },
  pitchOffsetSemitones: { min: -12, max: 12 },
  loopSeconds: { min: 0, max: 86400 },
};

// Frozen so a buggy merge in room.js can never mutate the shared default; room.js
// takes its own shallow copy per deck.
export const DEFAULT_DECK_STATE = Object.freeze({
  trackId: null,
  playing: false,
  positionSeconds: 0,
  gain: 1.0,
  playbackRate: 1.0,
  pitchOffsetSemitones: 0,
  loop: null,
});
