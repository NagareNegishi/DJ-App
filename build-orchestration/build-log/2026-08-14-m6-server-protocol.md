# M6 — sync server and protocol fixtures

Built `server/` and the shared wire-contract corpus. No client source changed; the only
client-side additions are a fixture test and one CMake line. Both suites are green (631
server tests, 164 client tests, up from 153) and the two-client smoke test passes. The
whitebox pass and the review layer did **not** run, so M6 is not recorded complete in
`PROGRESS.md`. That is the next session's first job.

## How the server is put together

Five modules, cut so that each layer can be tested without the one below it.

`protocol.js` holds constants and nothing else, and every one of them can be checked line
by line against a table in `02-protocol.md`. That property is the reason `DEFAULT_HOST` and
`DEFAULT_PORT` live in `config.js` instead: a bind address is deployment, not wire contract,
and mixing the two would cost the file its one reviewable invariant.

`validate.js` is pure and rejects rather than coerces. `room.js` is the domain and never
imports `ws`, so the black-box suite drives it with fake connection objects and no network
at all. `server.js` owns everything about sockets, and `index.js` is the only file that
touches process-level state.

The seam between `room.js` and `server.js` is the one worth remembering. The room returns
`{ok:true}` or `{ok:false, errorCode}` — the domain fact, never a transport consequence.
`server.js` decides which error codes count toward the three-strike close, and that mapping
sits next to `MAX_CONSECUTIVE_INVALID` where someone changing the policy will find it. An
earlier draft had the room return a `countsAsInvalid` boolean; that put a socket-level
policy inside a file whose whole job is protocol rules, and produced a four-state return
where one state was meaningless.

Role is derived, not stored. `conn` has no `role` property even though `03-server.md`
describes one, because the room already tracks `controllerId` and two writers of one fact
will drift. Recorded in `DEVIATIONS.md`.

## Decisions

**A hello with no `protocolVersion` closes 4001, not 4000.** `02-protocol.md` says
"wrong/missing `protocolVersion` ⇒ close 4001", so the version check runs before the strict
key-set check. Without that ordering an absent field collapses into the generic shape
failure and closes 4000. It matters beyond correctness: M7 turns close codes into
user-facing text, and 4001 means "your app is too old" while 4000 means "bad handshake".

**`trackId` may not contain `..`, and the protocol document now says so.** The client has
rejected `.`, `..`, and embedded `..` since M1 as defence against a track id ever reaching a
path. Building the server to the published regex alone would have made it accept ids the
client refuses. Amended the Field reference and left the version at 1, because no wire
format that ever ran is invalidated — the document was describing the implementation
inaccurately rather than specifying different behaviour, and there is no v1 peer for a v2 to
protect itself from.

**Fixtures declare where their payloads are.** The client cannot parse a `welcome` or a
`snapshot`; it parses the `PlaybackState` and `StateDelta` nested inside them, and the real
envelope parser does not exist until M7. Rather than teach the test to switch on message
type — a second parser that would silently drift from the real one — each fixture carries a
`payloads` array of RFC 6901 pointers and a payload type. The suite walks them and knows
nothing about envelopes. One exception survives: a wire delta nests changed fields under
`changes` while the client's `StateDelta` is flat, so the suite flattens. That single helper
is the only envelope knowledge left, and **M7 should route this suite through the real
parser and delete it.**

**Invalid fixtures name their expected outcome per consumer.** The two sides legitimately
disagree: the server rejects out-of-range values, the client clamps them. So `{"gain": 5.0}`
is invalid to one and fine to the other. Each invalid fixture carries an `expect` object
with no default, because the failure mode worth guarding against is an author forgetting the
field, and a default would silently assert the naive "the client rejects everything invalid"
rule that is wrong for exactly the cases the protocol cares most about.

**`DJ_ROOM_CODE` has a 16-character floor; failed handshakes are not throttled.** The room
code is the only thing gating membership once `HOST` leaves loopback, and each guess costs an
attacker one connection and one small frame, so online guessing is unbounded. A per-address
throttle was considered and rejected: it is defeated by source-address rotation, needs its
own eviction policy to avoid becoming a memory-growth path, would lock out several people
behind one NAT address, and its threshold would have to be disabled across the integration
suite, leaving the shipped configuration the least tested. The length floor is not an entropy
measurement. It is the point where an operator has to reach for a passphrase instead of a
word. Revisit at M7, when clients actually connect across machines.

**An oversize outbound frame closes 1011 rather than being dropped.** Dropping leaves a
client connected past its own handshake with default state and no error on either side. A
frame we cannot represent on the wire means our own state is unrepresentable, which is
programmer error, and the close is at least observable and recoverable through the reconnect
button M7 ships.

## What the specification review changed before anything was built

Reviewing the unit specs before writing code, rather than the code afterwards, is what this
session was structured around, and it paid for itself. Four things it caught:

Containment was scoped to three of the five message-pipeline steps while `index.js` exits
the process on `uncaughtException`. A throw in the binary check, the rate limiter, a timer
callback, or a heartbeat tick would have killed the room for all eight users from one
connection's input — the exact asymmetry `03-server.md` §9 forbids.

WebSocket handshakes are exempt from the same-origin policy, so binding to loopback does not
stop a web page loaded on the operator's own machine from opening a connection and guessing
the room code at browser speed. The server now refuses any upgrade carrying an `Origin`
header; IXWebSocket sends none, so it costs nothing today.

The rate limiter replied with an `error` frame per dropped message, so exceeding the limit
made the server do more work and send more bytes than staying under it. Its ban timer also
reset on a single served message, which one pause of about 17 milliseconds defeats.

`maxClients` is only checked after a hello validates, leaving pre-hello sockets unbounded.

The design review separately found that M6's acceptance requires a client-side fixture suite
that none of the four planned units owned, and that no spec pinned how a Catch2 binary would
locate the corpus.

## Accepted risk

- `requestSnapshot` is answered every time, with no deduplication, because a client asking
  for state is the defensive resync path and must get an answer. Bounded only by the
  per-connection token bucket. The repair snapshot on a rejected non-controller delta *is*
  deduplicated per connection per `serverSeq`.
- Failed handshakes are unthrottled per source address, as above.
- Two paths in `server.js` are written but never executed by any test: the 1 MB
  `bufferedAmount` backpressure ceiling, and the heartbeat reaper. `05-testing.md` exempts
  the heartbeat deliberately. The backpressure ceiling needs a peer that stalls its reader
  without closing, which is what the unrun whitebox pass was going to attempt.
- `02-protocol.md` does not state a starting value for `serverSeq`. The implementation starts
  at 0 and the suite asserts only that it is an integer and increments by exactly 1 per
  accepted delta, which is robust to the answer either way.
- CI has not run. The workflow's server job lost its pre-M6 existence guard and now runs
  `npm ci` and `npm test` for real against pinned Node 22, but nothing has been pushed.

## Note for whoever runs the review layer next

The four implementer reports each carried open items, all resolved or recorded except the
ones listed above. Two are worth re-reading before the review: `server.js`'s readiness
protocol resolved to a promise-returning `createServer` (the spec allowed either that or a
`ready` property, and the test helper accepts both, so nothing pins it), and `verifyClient`
refuses with an explicit `done(false, 403)` because returning `false` makes `ws` answer 401
rather than the 403 the spec called for.
