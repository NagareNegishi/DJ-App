# 02 — Wire Protocol (v2)

Single source of truth for client↔server communication. Server validation (`03-server.md`), client model serialization (`04-client.md`), and shared fixtures (`05-testing.md`) all implement exactly this document. If the protocol must change, bump the version, update this file first, then both implementations and fixtures in the same milestone.

## Transport

- WebSocket, **text frames only**, one JSON object per frame, UTF-8.
- Max frame size: **4096 bytes** (`ws` `maxPayload`; client enforces before send). Oversize ⇒ connection closed 1009.
- Binary frames ⇒ close 1008.
- Prototype uses `ws://` on a trusted LAN; production path is `wss://` via reverse proxy (see `06-security.md`). No credentials beyond the room code.
- Keepalive: server sends WebSocket ping every 15 s; terminates a connection with no pong for 30 s. Client (IXWebSocket) enables automatic pong and its own ping at 15 s.

## Envelope

Every message: `{ "type": "<string>", ... }`. Unknown `type` ⇒ server replies `error{code:"bad-message"}` and ignores it; three consecutive invalid messages ⇒ close 1008. Unknown *fields* inside known messages are rejected (strict validation), not ignored.

## Handshake sequence

```
client → server   (within 5 s of TCP open, else close 4000)
  {"type":"hello","protocolVersion":2,"name":"nagare","room":"<room code>"}

server → client   (on success)
  {"type":"welcome","clientId":"c-3f2a","role":"observer","serverSeq":42,
   "snapshot":{"decks":{"A":{<full PlaybackState>}}},
   "peers":[{"clientId":"c-9b01","name":"aki","role":"controller"}],
   "source":"https://github.com/NagareNegishi/DJ-App"}

server → all others
  {"type":"peerJoined","clientId":"c-3f2a","name":"nagare","role":"observer"}
```

Failure closes: wrong/missing `protocolVersion` ⇒ close **4001**; wrong `room` ⇒ close **4004**; room full ⇒ close **4002**; malformed hello / timeout ⇒ close **4000**.

## Messages: client → server

| type | fields | notes |
|---|---|---|
| `hello` | `protocolVersion` (int, must be 2), `name` (string 1–32, printable, no control chars), `room` (string 1–64) | Must be first message; anything else first ⇒ close 4000 |
| `claimControl` | — | Grants controller role if currently unclaimed; otherwise `error{code:"control-taken"}` |
| `releaseControl` | — | Only valid from current controller; otherwise `error{code:"not-controller"}` |
| `delta` | `deck` ("A"\|"B"), `changes` (object, ≥1 field from Field reference) | Only accepted from controller. From others ⇒ `error{code:"not-controller"}` **followed by a `snapshot`** (to repair their optimistic state) |
| `requestSnapshot` | — | Server replies with `snapshot` |

## Messages: server → client

| type | fields | notes |
|---|---|---|
| `welcome` | `clientId`, `role`, `serverSeq`, `snapshot`, `peers[]`, `source` (string) | Once, in response to valid hello. `source`: AGPL §13 corresponding-source pointer; sent once per connection |
| `delta` | `serverSeq` (int, monotonic per room), `sourceClientId`, `deck`, `changes` | Broadcast to **all clients except the source** (source already applied optimistically) |
| `snapshot` | `serverSeq`, `decks:{A:{...},B?:{...}}` | Full canonical state; recipient replaces local state wholesale |
| `roleChanged` | `clientId`, `role` ("controller"\|"observer") | Broadcast to everyone incl. subject, on claim/release only |
| `peerJoined` | `clientId`, `name`, `role` | Broadcast to others |
| `peerLeft` | `clientId` | Broadcast to others |
| `error` | `code`, `message` (human-readable, no internals) | Non-fatal; connection stays open unless stated |

`serverSeq` increments on every state-mutating accepted message. Clients track the last seen `serverSeq`; if a received value is not strictly greater, send `requestSnapshot` (defensive resync; shouldn't happen over TCP).

## Error codes

`bad-message` (schema/size/type violation) · `not-controller` · `control-taken` · `rate-limited` · `unknown-track` (reserved) . Close codes: **4000** bad handshake, **4001** version mismatch, **4002** room full, **4003** rate-limit ban, **4004** wrong room code, **1008** policy violation, **1009** oversize frame, **1001** graceful shutdown, **1011** internal error.

## Field reference — `PlaybackState` / `changes` (THE canonical table)

| field | JSON type | valid range | default | notes |
|---|---|---|---|---|
| `trackId` | string | `^[A-Za-z0-9._-]{1,64}$` or `null`, and additionally **not** `"."`, `".."`, or any value containing `".."` | `null` | id from repository manifest; clients that don't have this id show "missing track" and stay silent, state still applies. The `".."` exclusion is not expressible in the character-class regex and is checked separately: a `trackId` must never be usable as a path segment (`06-security.md` §Client rules 2). Both sides enforce it |
| `playing` | boolean | — | `false` | a delta setting `playing:true` MUST also carry `positionSeconds` |
| `positionSeconds` | number | finite, `0 ≤ x ≤ 86400` | `0` | client additionally clamps to track duration |
| `gain` | number | finite, `0.0 ≤ x ≤ 2.0` | `1.0` | linear amplitude |
| `playbackRate` | number | finite, `0.5 ≤ x ≤ 2.0` | `1.0` | prototype: rate affects pitch (no timestretch) |
| `pitchOffsetSemitones` | number | finite, `-12 ≤ x ≤ 12` | `0` | **accepted, stored, synced, but not rendered** until TimeStretcher exists (M10); UI does not expose it before then |
| `loop` | object or null | `{"inSeconds":a,"outSeconds":b}`, finite, `0 ≤ a < b ≤ 86400` | `null` | `null` clears the loop |
| `repeat` | boolean | — | `true` | whole-track auto-replay on reaching end-of-track; `false` restores the pre-M9 self-stop at end |

Validation policy: **server rejects** out-of-spec values (`error{bad-message}`, delta dropped whole — no partial application); **client clamps** incoming values into range as defense-in-depth, and clamps its own outgoing values before send. Numbers must be JSON numbers, finite (reject `NaN`/`Infinity` — JSON.parse already excludes them; client-side parser must too).

## Rate limiting

Token bucket per connection: **60 messages/s sustained, burst 120**. On exhaustion the message is dropped, and the connection gets **one** `error{code:"rate-limited"}` per deficit episode — not one per dropped message. Answering every dropped frame would make the limiter send more bytes than it receives, so a flood would cost the server more than the flooder. The episode ends as soon as a message is served again. Continuous violation for > 5 s ⇒ close 4003. Client-side: UI slider streams are throttled to ≤ 30 deltas/s per control (trailing-edge coalescing), keeping normal use far under the limit.

## Room and control model

- One room per server process. Room code generated at startup (`crypto.randomUUID()`) and printed to stdout, overridable via `DJ_ROOM_CODE` env var.
- Max **8** clients (`DJ_MAX_CLIENTS`).
- Exactly zero or one controller. First `claimControl` wins. Controller disconnect ⇒ control becomes unclaimed; no `roleChanged` is sent for the departing client, because `peerLeft` already removes them from every peer list and no other peer's role changes. A client determines "no controller is currently held" by the absence of any peer with `role: "controller"` after processing `peerLeft` — not from a dedicated signal. No auto-promotion — a human clicks "claim".
- Server state per deck starts at Field-reference defaults; deck `"B"` appears in snapshots only after the first delta touches it.
