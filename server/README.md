# DJ App sync server

A WebSocket relay that holds the canonical playback state for one room and keeps every
connected client in agreement about it. It decides who is allowed to control playback,
validates every message against the wire protocol, and broadcasts accepted changes to
everyone else.

It never touches audio. Each client plays its own local copy of the files, and only state
crosses the network. There is no database and nothing is written to disk.

The wire protocol is specified in `docs/plan/02-protocol.md`, which is the single source of
truth for both this server and the C++ client. The server's own design is in
`docs/plan/03-server.md`.

## Running it

```bash
cd server
npm ci
npm start
```

On startup it prints the room code and a JSON log line naming the address it bound to:

```
room code: 3f2ac1d4-...
{"ts":"2026-08-14T19:04:11.902Z","level":"info","event":"listening","host":"127.0.0.1","port":8765}
```

The room code is what clients need to join. It is printed once and never appears in a log
line again.

Stop it with Ctrl-C. It closes every connection with code 1001 and exits 0.

## Tests

```bash
npm test
```

Unit tests drive the domain layer with fake clients and no sockets. Integration tests start
a real server on an ephemeral port and connect real WebSocket clients to it. Both run in the
dev container and in CI. Nothing here needs an audio device.

## Environment variables

| Variable | Default | Notes |
|---|---|---|
| `HOST` | `127.0.0.1` | Bind address. See the note below before changing it. |
| `PORT` | `8765` | `0` asks the OS for an ephemeral port, which the tests use. |
| `DJ_ROOM_CODE` | a fresh UUID | Must be 16 to 64 characters if you set it. |
| `DJ_MAX_CLIENTS` | `8` | Must be at least 1. |
| `LOG_LEVEL` | `info` | One of `error`, `warn`, `info`, `debug`. |

A malformed value stops the server at startup with a message naming the variable and the
constraint it broke. It will not fall back to a default, because a typo in `PORT` should be
loud rather than quietly ignored.

The rate limiter is deliberately not configurable from the environment, so that it cannot be
switched off by accident. Tests that need a smaller token bucket pass a config object
directly to `createServer`.

### Why `DJ_ROOM_CODE` has a minimum length

Once `HOST` is set to anything other than loopback, the room code is the only thing standing
between an uninvited peer and full membership. Failed handshakes are not rate limited, and
each guess costs an attacker one connection and one small frame, so a short code like `party`
would not survive long on a shared network. Sixteen characters is not a measurement of
entropy. It is the point where you have to reach for a passphrase instead of a word, and
something like `purple-dragon-42` is both long enough and still easy to read out loud.

The generated default is a UUID and needs no such help.

## Production posture

This is a prototype and it is built for a trusted local network. Two things need to change
before it faces anything else, and neither is built here.

**Terminate TLS at a reverse proxy.** Run nginx or Caddy in front, serve `wss://` from it,
and have it forward to this server on localhost. Keep `HOST=127.0.0.1` so the process itself
is only reachable through the proxy. The server speaks plain `ws://` on purpose and should
not be exposed directly.

**Reconsider the `Origin` check.** The server currently refuses any upgrade request that
carries an `Origin` header at all. WebSocket handshakes are exempt from the same-origin
policy, so without this any web page loaded on the operator's machine could open a
connection to the server on loopback. The real client is IXWebSocket, which sends no
`Origin`, so the check costs nothing today. If a browser ever becomes a legitimate client,
this has to become an allowlist rather than a blanket refusal.

Some things are known gaps rather than oversights, and they are recorded so the next person
does not have to rediscover them:

- Failed handshakes are not throttled per source address. The 16-character floor on
  `DJ_ROOM_CODE` is the mitigation instead. A per-address throttle was considered and left
  out: it is defeated by address rotation, needs its own eviction policy to avoid becoming a
  memory leak, and would lock out several people sharing one NAT address.
- `requestSnapshot` is answered every time it is asked, with no deduplication, because a
  client asking for state is the defensive resync path and has to be able to get an answer.
  It is bounded only by the per-connection rate limit.

## Logging

One JSON object per line, on stdout. Nothing is written to a file.

Startup, joins, role changes, departures, and close codes are logged at `info`. Accepted
deltas are logged at `debug` and record which fields changed but never their values. The
room code is never logged after the startup line, and message bodies are never logged at
`info`.
