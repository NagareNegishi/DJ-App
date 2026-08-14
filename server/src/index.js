// Entry point: composes config, logger, room and server, prints the room code for
// the operator, and owns every process-level concern — signals and the fail-loud
// handlers. Nothing else in the server touches process state.

import process from 'node:process';

import { loadConfig } from './config.js';
import { createLogger } from './log.js';
import { PROTOCOL_VERSION } from './protocol.js';
import { createRoom } from './room.js';
import { createServer } from './server.js';

let config;
try {
  config = loadConfig(process.env);
} catch (err) {
  // No logger yet, and a bad environment is an operator error, not a log record.
  process.stderr.write(`configuration error: ${err.message}\n`);
  process.exit(1);
}

const logger = createLogger({ level: config.logLevel });

// Fail loud on programmer error, never on client input (03-server.md §9). These
// belong here and only here: server.js is a library module that an integration
// test imports many times over, and every path a frame can reach is contained
// before it could ever get this far.
process.on('uncaughtException', (err) => {
  logger.error('uncaught-exception', { message: err?.message });
  process.exit(1);
});

process.on('unhandledRejection', (reason) => {
  logger.error('unhandled-rejection', { message: reason?.message ?? String(reason) });
  process.exit(1);
});

const room = createRoom({
  roomCode: config.roomCode,
  maxClients: config.maxClients,
  logger,
  protocolVersion: PROTOCOL_VERSION,
});

let server;
try {
  server = await createServer({ config, logger, room });
} catch (err) {
  process.stderr.write(`cannot listen on ${config.host}:${config.port}: ${err.message}\n`);
  process.exit(1);
}

// Operator output rather than a log record — this is the line a user reads to
// join, and the room code must never appear in a log.
console.log(`room code: ${config.roomCode}`);

let shuttingDown = false;

const shutdown = async (signal) => {
  // A second Ctrl-C while the first close is in flight must not start a second
  // teardown.
  if (shuttingDown) return;
  shuttingDown = true;
  logger.info('shutdown', { signal });
  await server.close();
  process.exit(0);
};

const onSignal = (signal) => () => {
  shutdown(signal).catch((err) => {
    logger.error('shutdown-failed', { message: err?.message });
    process.exit(1);
  });
};

process.on('SIGINT', onSignal('SIGINT'));
process.on('SIGTERM', onSignal('SIGTERM'));
