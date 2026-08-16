# M7 — multi-user sync on one deck

Two app instances, one controller, shared state, audible on both. The product's core
promise. Landed in four units (protocol amendments, `PositionClock`, `WebSocketTransport`,
`ConnectPanel`/composition-root wiring), then a whitebox pass, then a four-adviser review
round that found four real bugs and fixed all of them in the same session. Container green:
233 client tests (up from 164), 668 server tests (up from 667), clean build, client runs
without crashing.

## The three M6 carryovers

M6's review left three items for M7 to pick up. All three landed as part of the first unit,
before any client code touched the network.

`roleChanged` on controller disconnect. The protocol text was self-contradictory: it said
`roleChanged` fires on disconnect while also saying the departed id's new role "is implied
by `peerLeft`." The server sent it anyway, after `peerLeft`, so a client that processes
`peerLeft` first would get a `roleChanged` for an id it had already removed. Pinning an
ordering would just relocate the hazard, so the broadcast was removed entirely instead.
Nobody's role actually changes when a controller leaves. The controller slot just sits
empty until claimed. `02-protocol.md` now says so plainly, and a client determines "no
controller held" from the absence of any peer with that role, not from a dedicated signal.

`welcome.source` for AGPL §13, added as a field on `welcome`, server-side, pointing at the
public repository. Static string for now (see Decisions).

Room code and unthrottled handshakes: investigated and found already resolved. Last
session's `server/README.md` already carries a fully-reasoned rejection of per-address
handshake throttling (defeated by address rotation, needs its own eviction policy, locks
out NAT-shared users), plus an existing global pre-hello connection ceiling. Nothing about
clients connecting across machines changes that reasoning, so nothing was built. The
carryover closes as "already correctly documented," the same resolution the M6 review
itself used for the equivalent server-side question.

## The four units

Protocol and server (`02-protocol.md`, `room.js`, fixtures): the two amendments above.

`state/PositionClock`: the controller's 5 s position resync, additive only. It didn't touch
`EngineAdapter`'s own self-stop poll, despite a standing comment inviting exactly that
consolidation. The two timers run at different frequencies for different reasons (10 Hz
responsiveness vs. 5 s resync), so merging them would have been a real regression in
self-stop detection latency, not a simplification.

`sync/WebSocketTransport`: the real transport, IXWebSocket-backed. This got a design and
security review of the spec before any code existed, which was worth doing again for a
unit this size. That pass caught a duplicate parser: `ProtocolFixtureTest.cpp` already had
a private `flattenWireDelta` stand-in with a comment reading "when the real parser lands,
route this suite through it and delete this helper." The first draft of this unit's spec
proposed building the same logic again from scratch, in a new `sync/ProtocolMessages`
module. That missed both the prior art and that `04-client.md` already assigns "whole
protocol messages" to `model/Serialization.h/.cpp`, not a new module. The revised spec
folded the new envelope-building/parsing functions into `Serialization` and retired the
stand-in. Other fixes the pre-implementation review forced before any code was written:
IXWebSocket's default auto-reconnect had to be explicitly disabled, since it would have
silently defeated "no auto-reconnect loop, reconnect is a UI button." The alive-flag
lifecycle needed destructor and re-entrant-`connect()` handling that neither draft had. The
server's free-text close reason gets discarded rather than surfaced, because every
documented close code already has one complete, trustworthy meaning and the server's own
accompanying string does not.

`ui/ConnectPanel`, role-gating, and composition-root wiring: the integration unit. No
automated tests are possible or expected here (`ui/`/`app/` never link `juce_gui_basics`
into the test binary, an established, deliberate gap, verified by host checklist instead,
matching `07-milestones.md`'s own "integration is manual" for M7). The implementer caught
one real gap in its own spec on its own: `SyncPublisher::setRole` was never mentioned in
the spec's wiring snippet, which would have left a controller's own deltas never forwarded.

## The review round

Four advisers ran over the full landed diff: correctness, security, legal, change-discipline.
Three of them independently converged on the same missing piece: `SyncPublisher`'s ≤30/s
outbound throttle, promised by `04-client.md` and by that class's own M4-era comment
("lands with WebSocketTransport at M7"), never actually built by any of the four units.
Nobody had assigned it anywhere. Without it, dragging a gain or rate slider for more than
about two seconds could exceed the server's 60/s sustained rate limit and get the
controller banned mid-drag. Not a theoretical attack, an ordinary-use bug the M7 host
checklist's own Step 8 would likely have hit.

The one that mattered most: a user clicking Disconnect left the client stuck showing
"connected" forever, with no way back to a usable state short of restarting the app.
`WebSocketTransport::teardown()` flips its alive flag false before stopping the socket,
correct for discarding a stale server-initiated Close, but it also means a self-initiated
disconnect never produces a Close event the guard would let through. So `onConnectionChange`
never fired for the one disconnect path the user actually controls. Every other disconnect
(server drop, ban, version mismatch) worked correctly. Only the button did not.

Also found: a client missing the controller's track kept playing its own stale, previous
track's audio at the new position instead of going silent, a direct violation of the
milestone's own acceptance criteria, present since M4/M5's `EngineAdapter` but only
reachable over a real network starting this milestone. And a real gap between what
`M7-host.md` (drafted this session) said should happen and what the code did: deck controls
stayed locked for a connected observer even when nobody at all held control, and never
re-evaluated when a controller released or disconnected. Fixed by deriving "is anyone
controlling" from the peer list rather than from local role alone, and by calling the
enablement refresh from the `peerLeft` and other-peer-`roleChanged` paths, which previously
never triggered it.

Three fix units ran in parallel against disjoint files (composition-root/UI, transport,
`SyncPublisher`), each reviewed diff by diff before merging. All four high-severity
findings landed, plus seven medium/low findings. Final suites: 233/233 client, 668/668
server.

## Decisions

Per-deck throttle coalescing, not per-control. `04-client.md`'s literal text says "per
control." Coalescing by deck instead (`SyncPublisher`'s natural granularity) is a strictly
tighter bound. It satisfies the actual requirement, never trip the server's limiter from UI
dragging, with far simpler state than tracking a separate budget per field, since a delta
carries no notion of which control produced it, only which fields changed.

The client-side inbound frame-size cap doesn't actually bound memory. IXWebSocket buffers a
full frame (or an unbounded fragment chain) before the app ever sees a byte, and exposes no
max-payload setting, so the transport's 4096-byte check runs on an already-fully-allocated
string. A single giant frame or endless fragment chain from a malicious or compromised
server can still exhaust client memory. No partial fix was built: closing the fragment-chain
half while leaving the single-giant-frame half open would add real complexity for
incomplete protection. `06-security.md` now says so explicitly, instead of implying the
"giant frames" defense covers the client the way it covers the server. Accepted risk, same
class as the two items below.

No client-side inbound rate limiting, no liveness watchdog. Both considered and declined.
`06-security.md`'s own threat table assigns exactly one defense to a malicious or
compromised server, client-side validation and clamping, not flood defense, and explicitly
scopes "DoS beyond per-connection limits" out of this prototype. Building either would be
gold-plating beyond what the project's own security document commits to.

`welcome.source` is a static repository URL, not tied to the actually-running commit. A
real AGPL §13 gap, but only if an operator patches the server and forgets to update the
constant, which is already the documented, manual mitigation in `server/README.md` ("if you
have patched anything, publish your patched version and point them at that instead").
Accepted as-is for the prototype rather than building commit-SHA-embedding automation.

No `DEVIATIONS.md` entries for the two `02-protocol.md` edits. That file is for cases where
the plan turned out to be wrong or impossible and something else was done instead. Both M7
protocol edits are ordinary iterative protocol design, exactly what "change the protocol
doc first" is for, not a deviation from an instruction that couldn't be followed. This
progress line is the record instead.

## Also fixed, from the review round

Server-controlled free text (the `error` frame's `message` field) was being logged verbatim
by `MainComponent` with no guard, defeating the log-once, no-raw-payload discipline
`WebSocketTransport` already established one layer below for exactly this reason. It now
logs a fixed classification plus the whitelisted `code` only, once per connection. Inbound
peer records (`welcome.peers`, `peerJoined`) weren't validated or bounded before being
stored and rendered, so a forged name with embedded newlines could fake a peer-list entry
and nothing capped list growth. They're now validated against the same shape rule outbound
hello names get, capped at the protocol's 8-client max, deduped by id. The drift-resync
`requestSnapshot` fired before the inbound delta was even validated, so a malformed-frame
flood reflected 1:1 back at the server fast enough to risk tripping its own rate limiter.
Reordered to validate first. An invalid-hello notification bypassed the alive-flag guard
every other marshalled callback in the transport uses, flagged independently by two
reviewers, and is now routed through the same guard. The room code field was plain,
unmasked text for the whole session despite being the system's only access credential, and
is now password-masked. A missing third-party notices file for IXWebSocket's BSD-3
attribution requirement was added (`THIRD_PARTY_NOTICES.md`).

## Process note

Early in the session, `agent-worktree.sh add`'s second argument (test-directories-to-prune,
for keeping a client implementer blind to the tests it'll be graded against) was misused
for a server-only unit, passing it the unit's source paths instead. The sparse-checkout
excluded exactly the files the implementer needed to edit. The implementer worked around it
by reconstructing the missing files from a sibling worktree, which worked because both
worktrees shared a base commit. But the workaround meant the manager's `git status` in that
worktree never showed those files as changed (skip-worktree suppresses that), and the first
`merge` silently dropped them. Caught by diffing the worktree against its own base after the
fact, recovered with a patch applied directly to the main tree. No unit after the first used
the tool this way again. Worth remembering: `add`'s test-dirs argument prunes test
directories specifically (`client/tests/...`), not an arbitrary source-file allowlist. For a
unit with nothing to prune, most server-only units, pass nothing, not the unit's own file
list.
