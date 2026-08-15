// Unit tests for server/src/log.js: one JSON line per call, level filtering by the
// documented numeric map, and a serialization failure that never escapes the logger.

import test from 'node:test';
import assert from 'node:assert/strict';

import { createLogger } from '../src/log.js';

const FIXED_NOW = new Date('2024-05-06T07:08:09.010Z');
const now = () => FIXED_NOW;

function sink() {
  const lines = [];
  return { lines, write: (line) => lines.push(line) };
}

const LEVELS = ['error', 'warn', 'info', 'debug'];
const RANK = { error: 0, warn: 1, info: 2, debug: 3 };

test('a logger exposes exactly the four level methods', () => {
  const logger = createLogger({ write: sink().write });

  for (const level of LEVELS) {
    assert.equal(typeof logger[level], 'function', `${level} should be a function`);
  }
});

test('a call emits one line of JSON carrying ts, level, event and the given fields', () => {
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });

  logger.info('listening', { host: '127.0.0.1', port: 8765 });

  assert.equal(out.lines.length, 1);
  assert.equal(
    out.lines[0],
    `${JSON.stringify({
      ts: FIXED_NOW.toISOString(),
      level: 'info',
      event: 'listening',
      host: '127.0.0.1',
      port: 8765,
    })}\n`,
  );
});

test('a call with no fields still emits ts, level and event', () => {
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });

  logger.info('listening');

  assert.deepEqual(JSON.parse(out.lines[0]), {
    ts: FIXED_NOW.toISOString(),
    level: 'info',
    event: 'listening',
  });
});

test('the timestamp comes from the injected clock as an ISO string', () => {
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });

  logger.warn('rate-limited', {});

  assert.equal(JSON.parse(out.lines[0]).ts, '2024-05-06T07:08:09.010Z');
});

for (const configured of LEVELS) {
  for (const called of LEVELS) {
    const emits = RANK[called] <= RANK[configured];
    test(`at level ${configured} a ${called} call ${emits ? 'emits' : 'is suppressed'}`, () => {
      const out = sink();
      const logger = createLogger({ level: configured, write: out.write, now });

      logger[called]('client-joined', { clientId: 'c-3f2a' });

      assert.equal(out.lines.length, emits ? 1 : 0);
    });
  }
}

test('the default level is info, so debug is suppressed', () => {
  const out = sink();
  const logger = createLogger({ write: out.write, now });

  logger.debug('delta-applied', { deck: 'A' });

  assert.equal(out.lines.length, 0);
});

test('a suppressed call returns before it builds its line', () => {
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });
  const fields = {
    get deck() {
      throw new Error('fields must not be read for a suppressed call');
    },
  };

  logger.debug('delta-applied', fields);

  assert.equal(out.lines.length, 0);
});

test('unserializable fields degrade to a minimal line instead of throwing', () => {
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });
  const circular = {};
  circular.self = circular;

  logger.error('handler-error', circular);

  assert.equal(out.lines.length, 1);
  assert.deepEqual(JSON.parse(out.lines[0]), {
    ts: FIXED_NOW.toISOString(),
    level: 'error',
    event: 'handler-error',
    logError: 'unserializable fields',
  });
});

test('every emitted line ends with a newline', () => {
  const out = sink();
  const logger = createLogger({ level: 'debug', write: out.write, now });

  logger.debug('delta-applied', { deck: 'A', fields: ['gain'] });

  assert.equal(out.lines[0].endsWith('\n'), true);
  assert.equal(out.lines[0].trimEnd().includes('\n'), false, 'one line per call');
});

test('the logger applies no redaction of its own', () => {
  // Deciding what is safe to log is the caller's job (03-server.md §Logging); the sink
  // must pass fields through untouched.
  const out = sink();
  const logger = createLogger({ level: 'info', write: out.write, now });

  logger.info('client-joined', { remoteAddress: '192.168.1.9', name: 'aki' });

  assert.deepEqual(JSON.parse(out.lines[0]), {
    ts: FIXED_NOW.toISOString(),
    level: 'info',
    event: 'client-joined',
    remoteAddress: '192.168.1.9',
    name: 'aki',
  });
});
