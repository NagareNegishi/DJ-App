# 03 — Sync Server (Node.js)

A small, dependency-light WebSocket relay that owns canonical room state and enforces the protocol in `02-protocol.md`. No audio processing, no persistence, no database.

## Stack

- Node.js **22 LTS**, ES modules (`"type": "module"`).
- Runtime dependency: **`ws` 8.x only.** No Express, no ajv, no lodash. Validation is hand-rolled and table-driven (the protocol is small; zero-dep beats schema-lib).
- Dev dependencies: none required (`node:test` + `node:assert` are built in). Do not add nodemon/eslint/prettier unless a milestone says so.
- `package-lock.json` committed. `npm ci` in CI.

## Files

```
server/
├── package.json            # name dj-app-server, private, scripts: start, test
├── src/
│   ├── index.js            # entry: read config from env, create room + server, print room code, handle SIGINT/SIGTERM graceful close
│   ├── config.js           # env parsing with defaults: HOST=127.0.0.1, PORT=8765, DJ_ROOM_CODE?, DJ_MAX_CLIENTS=8, LOG_LEVEL=info
│   ├── server.js           # ws.WebSocketServer wiring: maxPayload 4096, connection lifecycle, hello timeout, heartbeat ping/pong, per-connection rate limiter; parses JSON safely and hands valid envelopes to room
│   ├── room.js             # the domain: clients map, roles, canonical deck state, serverSeq, message handlers (hello/claim/release/delta/requestSnapshot), broadcast helpers
│   ├── validate.js         # pure functions: validateHello, validateDelta(changes), clampless strict checks per 02-protocol Field reference; returns {ok} | {ok:false, reason}
│   ├── protocol.js         # constants: PROTOCOL_VERSION, limits, error codes, close codes, field ranges (mirrors 02-protocol tables)
│   └── log.js              # single-line JSON logs to stdout: {ts, level, event, ...fields}; no message bodies at info level
└── test/                   # see 05-testing.md
```

Keep `room.js` free of `ws` imports — it operates on a thin `client` abstraction (`{id, name, role, send(obj), close(code)}`) injected by `server.js`. This is what makes the domain unit-testable without sockets.

## Behavior requirements

1. **Startup:** bind `HOST:PORT` (default `127.0.0.1:8765`); generate room code if `DJ_ROOM_CODE` unset; print `room code: <code>` and a ready line to stdout. Exit non-zero with a clear message if the port is taken.
2. **Connection:** attach a 5 s hello timer. First message must be a valid `hello` (protocol version, room code, name) or close per `02-protocol.md` close codes. On success: register client, assign `c-<4 hex>` id (collision-checked), send `welcome` with current snapshot + peers, broadcast `peerJoined`.
3. **Message handling:** every inbound frame goes through, in order: rate-limit check → size/UTF-8/JSON parse (wrap in try/catch; parse failure = `bad-message`) → envelope check → per-type validation (`validate.js`) → role check → apply to canonical state → increment `serverSeq` → broadcast per `02-protocol.md`. A failure at any step sends `error{...}` and **does not** partially apply anything. Three consecutive invalid messages ⇒ close 1008.
4. **Canonical state:** `room.decks` starts `{A: defaults}` (defaults from the Field reference). Applying a delta merges validated `changes` into the deck. `loop:null` clears loop. Deck `"B"` is created on first valid delta targeting it.
5. **Control:** enforce exactly-zero-or-one controller server-side. Deltas from non-controllers: `error{not-controller}` + `snapshot` to the sender only, nothing broadcast.
6. **Disconnect:** clear heartbeat, broadcast `peerLeft`; if the departing client was controller, unset controller and broadcast `roleChanged` semantics per protocol doc.
7. **Heartbeat:** ping all clients every 15 s; terminate any that missed the previous pong.
8. **Graceful shutdown:** on SIGINT/SIGTERM, close all connections with 1001, close the server, exit 0.
9. **Never crash on input.** Any per-connection error is contained to that connection. Add a `process.on('uncaughtException')`/`unhandledRejection` handler that logs and exits non-zero (fail loud on programmer error, never on client input).

## Logging

JSON lines to stdout only. Log at `info`: startup, join (id + name + remote address), role changes, leave, close codes. Log at `debug`: accepted deltas (deck + field names, **not** values), rate-limit hits. Never log the room code after startup, never log raw frames at info.

## npm scripts

```json
"scripts": {
  "start": "node src/index.js",
  "test": "node --test test/"
}
```
