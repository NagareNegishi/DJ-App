// Server configuration read from the environment, per docs/plan/03-server.md §1.
// A malformed value throws rather than falling back to a default: a typo in an operator's
// env must be loud, and the thrown message names the variable and constraint, never the value.

import { randomUUID } from 'node:crypto';

import { DEFAULT_MAX_CLIENTS, DEFAULT_RATE_LIMIT, ROOM_CODE_MAX_LENGTH } from './protocol.js';
import { LOG_LEVELS } from './log.js';

// Bind address and port are deployment settings, not part of the wire contract, so they
// live here rather than in protocol.js.
const DEFAULT_HOST = '127.0.0.1';
const DEFAULT_PORT = 8765;

// The app is specified for a room of up to DEFAULT_MAX_CLIENTS (8) users. maxClients is
// the resource-exhaustion defence (docs/plan/06-security.md) and server.js scales its
// pre-hello connection ceiling from it, so it must stay bounded rather than accepting
// anything up to Number.MAX_SAFE_INTEGER — an operator typo (a stray zero, a copy-pasted
// env) would otherwise silently remove both limits. 8x the default leaves headroom for
// larger sessions without defeating the defence.
const MAX_CLIENTS_CEILING = DEFAULT_MAX_CLIENTS * 8;

// An operator-chosen room code is the only thing gating membership once HOST is off
// loopback, and failed handshakes are not rate-limited. This is not an entropy measure; it
// is the length at which a passphrase is needed instead of a word.
const ROOM_CODE_MIN_LENGTH = 16;

// Rejects "8080abc" and "" that Number.parseInt would otherwise accept or turn into NaN.
const parseIntegerVar = (raw, min, max, constraint) => {
  const text = String(raw).trim();
  const parsed = /^[+-]?\d+$/.test(text) ? Number.parseInt(text, 10) : Number.NaN;
  if (!Number.isSafeInteger(parsed) || parsed < min || parsed > max) {
    throw new Error(constraint);
  }
  return parsed;
};

/**
 * Builds the frozen server config from an environment object.
 *
 * Reads only the `env` argument — never `process.env` directly — so tests can pass a plain
 * object. Throws on any malformed value.
 *
 * @param {Record<string, string|undefined>} [env]
 * @returns {Readonly<{host: string, port: number, roomCode: string, maxClients: number,
 *   logLevel: string, rateLimit: typeof DEFAULT_RATE_LIMIT}>}
 */
export function loadConfig(env = process.env) {
  const host = typeof env.HOST === 'string' && env.HOST.length > 0 ? env.HOST : DEFAULT_HOST;

  // Port 0 is allowed and meaningful: it asks the OS for an ephemeral port, which the
  // integration tests rely on.
  const port =
    env.PORT === undefined
      ? DEFAULT_PORT
      : parseIntegerVar(env.PORT, 0, 65535, 'PORT must be an integer between 0 and 65535');

  let roomCode;
  if (env.DJ_ROOM_CODE === undefined) {
    roomCode = randomUUID();
  } else {
    roomCode = String(env.DJ_ROOM_CODE);
    if (roomCode.length < ROOM_CODE_MIN_LENGTH || roomCode.length > ROOM_CODE_MAX_LENGTH) {
      throw new Error(`DJ_ROOM_CODE must be between ${ROOM_CODE_MIN_LENGTH} and ${ROOM_CODE_MAX_LENGTH} characters`);
    }
  }

  const maxClients =
    env.DJ_MAX_CLIENTS === undefined
      ? DEFAULT_MAX_CLIENTS
      : parseIntegerVar(
          env.DJ_MAX_CLIENTS,
          1,
          MAX_CLIENTS_CEILING,
          `DJ_MAX_CLIENTS must be an integer between 1 and ${MAX_CLIENTS_CEILING}`,
        );

  let logLevel;
  if (env.LOG_LEVEL === undefined) {
    logLevel = 'info';
  } else if (LOG_LEVELS.includes(env.LOG_LEVEL)) {
    logLevel = env.LOG_LEVEL;
  } else {
    throw new Error(`LOG_LEVEL must be one of ${LOG_LEVELS.join(', ')}`);
  }

  // Deliberately not overridable from the environment: an operator cannot accidentally
  // disable the limiter. Tests needing a smaller bucket build a config literal instead.
  return Object.freeze({ host, port, roomCode, maxClients, logLevel, rateLimit: DEFAULT_RATE_LIMIT });
}
