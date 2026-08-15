// Unit tests for server/src/config.js: env parsing, defaults from 03-server.md §1, and
// the loud-failure rule (a malformed variable throws, it never falls back to a default).

import test from 'node:test';
import assert from 'node:assert/strict';

import { loadConfig } from '../src/config.js';

const VALID_ROOM_CODE = 'purple-dragon-42';

test('an empty environment yields the documented defaults', () => {
  const config = loadConfig({});

  assert.equal(config.host, '127.0.0.1');
  assert.equal(config.port, 8765);
  assert.equal(config.maxClients, 8);
  assert.equal(config.logLevel, 'info');
});

test('the rate limit is always the protocol default, with no env override', () => {
  const config = loadConfig({ DJ_RATE_LIMIT: '1', RATE_LIMIT: '1' });

  assert.deepEqual(config.rateLimit, {
    sustainedPerSecond: 60,
    burst: 120,
    banAfterMs: 5000,
  });
});

test('the returned config is frozen', () => {
  const config = loadConfig({});

  assert.equal(Object.isFrozen(config), true);
  assert.throws(() => {
    config.port = 1;
  }, TypeError);
});

test('an unset DJ_ROOM_CODE yields a generated code of at least 16 characters', () => {
  const config = loadConfig({});

  assert.equal(typeof config.roomCode, 'string');
  assert.ok(config.roomCode.length >= 16, 'a generated room code should be unguessable');
});

test('a generated room code differs between two loads', () => {
  assert.notEqual(loadConfig({}).roomCode, loadConfig({}).roomCode);
});

test('an operator-supplied DJ_ROOM_CODE is used verbatim', () => {
  assert.equal(loadConfig({ DJ_ROOM_CODE: VALID_ROOM_CODE }).roomCode, VALID_ROOM_CODE);
});

test('a DJ_ROOM_CODE of exactly 16 characters is accepted', () => {
  const code = 'a'.repeat(16);
  assert.equal(loadConfig({ DJ_ROOM_CODE: code }).roomCode, code);
});

test('a DJ_ROOM_CODE of exactly 64 characters is accepted', () => {
  const code = 'a'.repeat(64);
  assert.equal(loadConfig({ DJ_ROOM_CODE: code }).roomCode, code);
});

test('a DJ_ROOM_CODE below 16 characters is refused', () => {
  assert.throws(() => loadConfig({ DJ_ROOM_CODE: 'a'.repeat(15) }), Error);
});

test('a DJ_ROOM_CODE above 64 characters is refused', () => {
  assert.throws(() => loadConfig({ DJ_ROOM_CODE: 'a'.repeat(65) }), Error);
});

test('a refused DJ_ROOM_CODE names the variable and the constraint but never the value', () => {
  const weak = 'party';
  assert.throws(
    () => loadConfig({ DJ_ROOM_CODE: weak }),
    (err) => {
      assert.ok(err.message.includes('DJ_ROOM_CODE'), err.message);
      assert.match(err.message, /16/);
      assert.equal(err.message.includes(weak), false, 'the value must not be echoed');
      return true;
    },
  );
});

test('HOST overrides the default bind address', () => {
  assert.equal(loadConfig({ HOST: '0.0.0.0' }).host, '0.0.0.0');
});

test('an empty HOST falls back to loopback', () => {
  assert.equal(loadConfig({ HOST: '' }).host, '127.0.0.1');
});

test('PORT is parsed as a base-10 integer', () => {
  assert.equal(loadConfig({ PORT: '9000' }).port, 9000);
});

test('PORT 0 is allowed and means an ephemeral port', () => {
  assert.equal(loadConfig({ PORT: '0' }).port, 0);
});

test('PORT 65535 is allowed', () => {
  assert.equal(loadConfig({ PORT: '65535' }).port, 65535);
});

for (const [label, value] of [
  ['above the maximum', '65536'],
  ['negative', '-1'],
  ['not a number', 'eight-thousand'],
]) {
  test(`a PORT that is ${label} throws rather than falling back`, () => {
    assert.throws(() => loadConfig({ PORT: value }), Error);
  });
}

test('a refused PORT names the variable and the constraint but never the value', () => {
  assert.throws(
    () => loadConfig({ PORT: '99999' }),
    (err) => {
      assert.ok(err.message.includes('PORT'), err.message);
      assert.equal(err.message.includes('99999'), false, 'the value must not be echoed');
      return true;
    },
  );
});

test('DJ_MAX_CLIENTS overrides the default room size', () => {
  assert.equal(loadConfig({ DJ_MAX_CLIENTS: '3' }).maxClients, 3);
});

test('DJ_MAX_CLIENTS of 1 is allowed', () => {
  assert.equal(loadConfig({ DJ_MAX_CLIENTS: '1' }).maxClients, 1);
});

for (const [label, value] of [
  ['zero', '0'],
  ['negative', '-2'],
  ['not a number', 'many'],
]) {
  test(`a DJ_MAX_CLIENTS that is ${label} throws rather than falling back`, () => {
    assert.throws(() => loadConfig({ DJ_MAX_CLIENTS: value }), Error);
  });
}

test('DJ_MAX_CLIENTS at the 64-client ceiling is accepted', () => {
  // The ceiling is 8x DEFAULT_MAX_CLIENTS (config.js:21); it also scales server.js's
  // pre-hello connection ceiling, so it stays bounded rather than accepting anything up
  // to Number.MAX_SAFE_INTEGER.
  assert.equal(loadConfig({ DJ_MAX_CLIENTS: '64' }).maxClients, 64);
});

test('a DJ_MAX_CLIENTS one above the 64-client ceiling throws', () => {
  assert.throws(
    () => loadConfig({ DJ_MAX_CLIENTS: '65' }),
    (err) => {
      assert.ok(err.message.includes('DJ_MAX_CLIENTS'), err.message);
      assert.match(err.message, /64/);
      return true;
    },
  );
});

for (const level of ['error', 'warn', 'info', 'debug']) {
  test(`LOG_LEVEL ${level} is accepted`, () => {
    assert.equal(loadConfig({ LOG_LEVEL: level }).logLevel, level);
  });
}

test('an unrecognised LOG_LEVEL throws rather than falling back to info', () => {
  // LOG_LEVEL used to fall back silently; it now throws like every other malformed
  // variable (config.js:78-85), naming the variable and the accepted set, because a
  // wrong-case value (LOG_LEVEL=DEBUG) otherwise starts at info with no explanation
  // for why the debug lines an operator asked for never show up.
  assert.throws(
    () => loadConfig({ LOG_LEVEL: 'chatty' }),
    (err) => {
      assert.ok(err.message.includes('LOG_LEVEL'), err.message);
      assert.match(err.message, /error, warn, info, debug/);
      return true;
    },
  );
});

test('an unset LOG_LEVEL still defaults to info without error', () => {
  assert.equal(loadConfig({}).logLevel, 'info');
});

test('a LOG_LEVEL differing only in case throws rather than being treated as valid', () => {
  assert.throws(() => loadConfig({ LOG_LEVEL: 'DEBUG' }), Error);
});

test('loadConfig reads only its argument, never process.env', (t) => {
  const previous = process.env.HOST;
  process.env.HOST = '10.9.8.7';
  t.after(() => {
    if (previous === undefined) delete process.env.HOST;
    else process.env.HOST = previous;
  });

  assert.equal(loadConfig({}).host, '127.0.0.1');
});
