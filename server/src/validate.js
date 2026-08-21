// Strict, pure validation of inbound client messages against docs/plan/02-protocol.md.
// No I/O, no state: every export rejects rather than coerces or clamps (clamping is the
// client's job) and returns either {ok:true, value} or {ok:false, reason}.

import { DECK_IDS, FIELD_RANGES, NAME_MAX_LENGTH, ROOM_CODE_MAX_LENGTH, TRACK_ID_PATTERN } from './protocol.js';

const MESSAGE_TYPES = ['hello', 'claimControl', 'releaseControl', 'delta', 'requestSnapshot'];

const FIELDLESS_TYPES = ['claimControl', 'releaseControl', 'requestSnapshot'];

// Iterated in this order so a message with several bad fields always reports the same one.
const CHANGE_FIELDS = [
  'trackId',
  'playing',
  'positionSeconds',
  'gain',
  'playbackRate',
  'pitchOffsetSemitones',
  'loop',
  'repeat',
];

const NUMERIC_CHANGE_FIELDS = ['positionSeconds', 'gain', 'playbackRate', 'pitchOffsetSemitones'];

// C0 controls, DEL, the C1 block, and Unicode format characters (category Cf: bidi
// overrides like U+202E, the U+2066-U+2069 isolates, the U+200B-U+200F zero-widths):
// "printable, no control chars" per 02-protocol.md. Cf chars are invisible but not
// caught by the C0/C1 ranges, and a name is relayed verbatim into every peer's list.
const CONTROL_CHARS = /[\u0000-\u001F\u007F-\u009F]|\p{Cf}/u;

const UNPAIRED_SURROGATE = /[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?:[^\uD800-\uDBFF]|^)[\uDC00-\uDFFF]/;

const ok = (value) => ({ ok: true, value });

const fail = (reason) => ({ ok: false, reason });

export const isPlainObject = (v) => typeof v === 'object' && v !== null && !Array.isArray(v);

const hasField = (obj, key) => Object.hasOwn(obj, key);

const hasExactKeys = (obj, keys) => {
  const own = Object.keys(obj);
  return own.length === keys.length && keys.every((k) => hasField(obj, k));
};

// A name is relayed verbatim to every peer, so an unpaired surrogate would put a frame that
// `ws` rejects as invalid UTF-8 on other people's connections and close them with 1007.
const isWellFormedString = (s) =>
  typeof s.isWellFormed === 'function' ? s.isWellFormed() : !UNPAIRED_SURROGATE.test(s);

const inRange = (n, { min, max }) => n >= min && n <= max;

const validateNumericField = (name, value) => {
  if (typeof value !== 'number' || !Number.isFinite(value)) return fail(`${name} must be a finite number`);
  if (!inRange(value, FIELD_RANGES[name])) return fail(`${name} out of range`);
  return ok(value);
};

const validateTrackId = (value) => {
  if (value === null) return ok(null);
  if (typeof value !== 'string' || !TRACK_ID_PATTERN.test(value)) return fail('invalid trackId');
  // Tighter than the protocol table's regex, matching the client's isValidTrackId: a trackId
  // must never be usable as a path segment (06-security.md §Client rules 2).
  if (value === '.' || value === '..' || value.includes('..')) return fail('invalid trackId');
  return ok(value);
};

const validateLoop = (value) => {
  if (value === null) return ok(null);
  if (!isPlainObject(value)) return fail('loop must be an object or null');
  if (!hasExactKeys(value, ['inSeconds', 'outSeconds'])) return fail('loop must have only inSeconds and outSeconds');

  const { inSeconds, outSeconds } = value;
  if (
    typeof inSeconds !== 'number' ||
    !Number.isFinite(inSeconds) ||
    typeof outSeconds !== 'number' ||
    !Number.isFinite(outSeconds)
  ) {
    return fail('loop bounds must be finite numbers');
  }
  if (!inRange(inSeconds, FIELD_RANGES.loopSeconds) || !inRange(outSeconds, FIELD_RANGES.loopSeconds)) {
    return fail('loop bounds out of range');
  }
  // Strict: a zero-length loop is not a loop.
  if (!(inSeconds < outSeconds)) return fail('loop inSeconds must be less than outSeconds');

  return ok({ inSeconds, outSeconds });
};

/**
 * Validates a `hello` handshake message against the room's expectations.
 *
 * The three reason strings 'malformed hello', 'protocol version mismatch' and 'wrong room'
 * are the contract room.js maps to close codes 4000 / 4001 / 4004, and the check order below
 * is what decides which one a given bad hello gets.
 *
 * @param {unknown} msg parsed message
 * @param {{protocolVersion: number, roomCode: string}} expected
 * @returns {{ok: true, value: {protocolVersion: number, name: string, room: string}} | {ok: false, reason: string}}
 */
export function validateHello(msg, expected) {
  if (!isPlainObject(msg) || msg.type !== 'hello') return fail('malformed hello');

  // Ahead of the key-set check on purpose: 02-protocol.md sends a *missing* protocolVersion to
  // close 4001, so an absent or unreadable version must not be swallowed as a shape failure.
  if (!hasField(msg, 'protocolVersion') || !Number.isInteger(msg.protocolVersion)) {
    return fail('protocol version mismatch');
  }
  if (msg.protocolVersion !== expected.protocolVersion) return fail('protocol version mismatch');

  if (!hasExactKeys(msg, ['type', 'protocolVersion', 'name', 'room'])) return fail('malformed hello');

  const { name, room } = msg;
  if (typeof name !== 'string' || name.length < 1 || name.length > NAME_MAX_LENGTH) return fail('malformed hello');
  if (CONTROL_CHARS.test(name) || !isWellFormedString(name)) return fail('malformed hello');

  if (typeof room !== 'string' || room.length < 1 || room.length > ROOM_CODE_MAX_LENGTH) return fail('malformed hello');
  if (room !== expected.roomCode) return fail('wrong room');

  return ok({ protocolVersion: msg.protocolVersion, name, room });
}

/**
 * Validates a delta's `changes` object field by field against the 02-protocol.md Field
 * reference. Exported on its own so each field can be unit-tested in isolation.
 *
 * @param {unknown} changes
 * @returns {{ok: true, value: object} | {ok: false, reason: string}}
 */
export function validateChanges(changes) {
  if (!isPlainObject(changes)) return fail('changes must be an object');

  const keys = Object.keys(changes);
  if (keys.length === 0) return fail('changes must not be empty');
  // Also what keeps __proto__/constructor/prototype out of the result.
  if (keys.some((k) => !CHANGE_FIELDS.includes(k))) return fail('unknown field in changes');

  // Assembled key by key rather than spread, so the returned object shares nothing with the
  // caller's and carries only fields that passed.
  const value = {};

  if (hasField(changes, 'trackId')) {
    const r = validateTrackId(changes.trackId);
    if (!r.ok) return r;
    value.trackId = r.value;
  }

  if (hasField(changes, 'playing')) {
    if (typeof changes.playing !== 'boolean') return fail('playing must be a boolean');
    value.playing = changes.playing;
  }

  if (hasField(changes, 'repeat')) {
    if (typeof changes.repeat !== 'boolean') return fail('repeat must be a boolean');
    value.repeat = changes.repeat;
  }

  for (const name of NUMERIC_CHANGE_FIELDS) {
    if (!hasField(changes, name)) continue;
    const r = validateNumericField(name, changes[name]);
    if (!r.ok) return r;
    value[name] = r.value;
  }

  if (hasField(changes, 'loop')) {
    const r = validateLoop(changes.loop);
    if (!r.ok) return r;
    value.loop = r.value;
  }

  // Field reference MUST: a peer starting playback without a position would drift.
  if (value.playing === true && !hasField(value, 'positionSeconds')) {
    return fail('playing:true requires positionSeconds');
  }

  return ok(value);
}

/**
 * Validates a `delta` message: deck id plus the normalized `changes` object.
 *
 * @param {unknown} msg
 * @returns {{ok: true, value: {deck: string, changes: object}} | {ok: false, reason: string}}
 */
export function validateDelta(msg) {
  if (!isPlainObject(msg) || msg.type !== 'delta') return fail('malformed delta');
  if (!hasExactKeys(msg, ['type', 'deck', 'changes'])) return fail('malformed delta');
  if (typeof msg.deck !== 'string' || !DECK_IDS.includes(msg.deck)) return fail('invalid deck');

  const changes = validateChanges(msg.changes);
  if (!changes.ok) return changes;

  return ok({ deck: msg.deck, changes: changes.value });
}

/**
 * Checks the shape common to every inbound message: an object with a known `type`, and no
 * fields at all on the message types that take none.
 *
 * @param {unknown} msg
 * @returns {{ok: true, value: {type: string}} | {ok: false, reason: string}}
 */
export function validateEnvelope(msg) {
  if (!isPlainObject(msg)) return fail('not an object');
  if (typeof msg.type !== 'string') return fail('missing type');
  if (!MESSAGE_TYPES.includes(msg.type)) return fail('unknown message type');
  if (FIELDLESS_TYPES.includes(msg.type) && !hasExactKeys(msg, ['type'])) return fail('unexpected fields');

  return ok({ type: msg.type });
}
