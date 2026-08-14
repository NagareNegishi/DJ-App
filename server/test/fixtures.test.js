// Fixture-driven contract tests: every shared client-to-server fixture is fed through
// validate.js. Per the consumer matrix in the protocol-fixtures spec, the server suite
// reads client-to-server/ only; server-to-client/ belongs to the C++ client suite.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, existsSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import { validateHello, validateDelta, validateEnvelope } from '../src/validate.js';

const CORPUS_ROOT = fileURLToPath(
  new URL('../../shared/protocol/fixtures/client-to-server/', import.meta.url),
);

// The room code every fixture is written against, per the fixture spec's conventions.
const FIXTURE_ROOM_CODE = 'demo-room';

// room.js maps these validate.js reasons onto close codes; the corpus declares the codes.
const CLOSE_CODE_FOR_REASON = {
  'protocol version mismatch': 4001,
  'wrong room': 4004,
};

function loadDirectory(name) {
  const dir = path.join(CORPUS_ROOT, name);
  if (!existsSync(dir) || !statSync(dir).isDirectory()) {
    throw new Error(`shared fixture directory is missing: ${dir}`);
  }
  const files = readdirSync(dir).filter((file) => file.endsWith('.json')).sort();
  if (files.length === 0) {
    throw new Error(`shared fixture directory holds no fixtures: ${dir}`);
  }
  return files.map((file) => ({
    file,
    path: path.join(dir, file),
    fixture: JSON.parse(readFileSync(path.join(dir, file), 'utf8')),
  }));
}

let validEntries = [];
let invalidEntries = [];
let loadError = null;

try {
  validEntries = loadDirectory('valid');
  invalidEntries = loadDirectory('invalid');
} catch (err) {
  loadError = err;
}

// A missing or empty corpus must fail loudly rather than pass on an empty iteration.
test('the shared client-to-server fixture corpus is present and non-empty', () => {
  if (loadError) throw loadError;
  assert.ok(validEntries.length > 0);
  assert.ok(invalidEntries.length > 0);
});

// Runs a client-to-server frame through the same validation the server applies to it.
function validateClientFrame(message, expected) {
  const envelope = validateEnvelope(message);
  if (!envelope.ok) return envelope;
  if (message.type === 'hello') return validateHello(message, expected);
  if (message.type === 'delta') return validateDelta(message);
  return envelope;
}

for (const { file, fixture } of validEntries) {
  test(`valid fixture ${file} carries the documented fixture fields`, () => {
    assert.equal(typeof fixture.description, 'string');
    assert.ok(fixture.description.length > 0);
    assert.notEqual(fixture.message, undefined);
  });

  test(`valid fixture ${file} is accepted by validate.js`, () => {
    // A valid hello names its own room code; the server it would meet is configured
    // with that code.
    const expected = {
      protocolVersion: 1,
      roomCode: typeof fixture.message?.room === 'string' ? fixture.message.room : FIXTURE_ROOM_CODE,
    };
    const result = validateClientFrame(fixture.message, expected);
    assert.equal(result.ok, true, `rejected with reason: ${result.reason}`);
  });
}

for (const { file, fixture } of invalidEntries) {
  test(`invalid fixture ${file} carries the documented fixture fields`, () => {
    assert.equal(typeof fixture.description, 'string');
    assert.ok(fixture.description.length > 0);
    assert.notEqual(fixture.message, undefined);
    assert.equal(typeof fixture.reason, 'string');
    assert.ok(fixture.reason.length > 0);
  });

  test(`invalid fixture ${file} declares expect.server with no default`, () => {
    assert.notEqual(fixture.expect, undefined, 'expect is required on every invalid fixture');
    assert.equal(fixture.expect.server, 'reject');
    assert.equal(
      Object.hasOwn(fixture.expect, 'client'),
      false,
      'the client suite does not read client-to-server fixtures',
    );
  });

  test(`invalid fixture ${file} carries no payloads key`, () => {
    assert.equal(Object.hasOwn(fixture, 'payloads'), false);
  });

  test(`invalid fixture ${file} is rejected by validate.js`, () => {
    const result = validateClientFrame(fixture.message, {
      protocolVersion: 1,
      roomCode: FIXTURE_ROOM_CODE,
    });
    assert.equal(result.ok, false, 'the server must reject this fixture');
    assert.equal(typeof result.reason, 'string');
  });

  if (Object.hasOwn(fixture, 'closeCode')) {
    test(`invalid fixture ${file} is rejected with the close code it declares`, () => {
      const result = validateClientFrame(fixture.message, {
        protocolVersion: 1,
        roomCode: FIXTURE_ROOM_CODE,
      });
      assert.equal(result.ok, false);
      const closeCode = CLOSE_CODE_FOR_REASON[result.reason] ?? 4000;
      assert.equal(closeCode, fixture.closeCode);
    });
  } else {
    test(`invalid fixture ${file} omits closeCode because it is not a hello`, () => {
      assert.notEqual(fixture.message?.type, 'hello');
    });
  }
}
