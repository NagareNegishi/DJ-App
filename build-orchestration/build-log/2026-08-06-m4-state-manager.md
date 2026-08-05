# M4 — StateManager

## What landed

`StateManager` (canonical per-deck `PlaybackState`, `applyDelta`/`getState`/
`addListener`/`removeListener`) and `EngineAdapter` (the listener that turns applied
deltas into `AudioEngine` calls) in `client/src/state/` and `client/src/engine/`.
`SyncTransport` (interface), `NullTransport`, and `SyncPublisher` (the anti-echo-loop
listener that will forward local deltas to the network from M7) in `client/src/sync/`.
`MainComponent`'s dev UI now routes every control (load, play/pause, seek, gain, rate)
through `StateManager.applyDelta` instead of calling the engine directly. The M3
`// M4 replaces` block is gone. 134 tests in `client/tests/`, `ctest` green.

## Why `EngineAdapter` depends on `AudioRepository`

Not stated explicitly in `04-client.md`'s brief description, decided while writing the
unit spec: `StateDelta::trackId` is just an id string, and audio never crosses the
network (only state does), so turning a track-id change into actual sound requires
resolving it through the repository first. That resolution has to happen identically
whether the delta came from the local controller (who picked the track) or, from M7 on,
a remote observer receiving the same `trackId` and needing to load *their own copy* of
the file. `EngineAdapter` is deck-scoped, not source-scoped, precisely so it doesn't
care which case it's in.

## The position-injection rule, and the bug it produced

`04-client.md` says a `playing:true` delta must carry position, and that `StateManager`
"injects current engine position" when a local delta omits it. `StateManager` has no
dependency on `engine/` by design (layer boundaries), so "injects current engine
position" can only mean the deck's own last-*stored* `positionSeconds`, not a live
read. That's what got built, and it's correct for every case except one: nothing
updates that stored value while the deck is actually playing (that's `PositionClock`'s
job, arriving at M7), so it goes stale the moment playback advances past wherever it
was last explicitly set.

`correctness-adviser` caught the consequence: pause partway through a track, press
Play again, and the resume delta had no `positionSeconds` of its own. `StateManager`'s
injection filled in the stale stored value (still 0, or wherever the track was last
seeked to), and `EngineAdapter` faithfully seeked the engine back there before
resuming. Same failure shape as the end-of-track bug fixed in `73075b9`, reintroduced
by M4 routing everything through a state layer that doesn't track live engine
position. Fixed at the one place that actually has both a live engine reference and
dev-UI license to read it directly: `MainComponent::togglePlayPause` now always
supplies `positionSeconds` from `engineA_.getCurrentPosition()` on the resume path
(falling back to `0.0` only in the pre-existing stuck-at-end-of-track case), so
`StateManager`'s stale-fallback injection is never exercised for that call site. The
plan doc's wording was reworded to match ("last-stored position," not "current engine
position") and to state explicitly that a caller with live engine access is
responsible for supplying the real value when it matters.

## A real re-entrancy bug in the notification loop

White-box testing found that `StateManager::applyDelta`'s notification loop (a plain
range-for over the `listeners_` map) segfaults if a listener removes its own
registration from inside its own callback: `removeListener`'s `erase()` destroys the
exact map node the range-for's iterator holds, and the next implicit increment is
undefined behavior. Reproduced directly against the real build. No current listener
(`EngineAdapter`, `SyncPublisher`) does this, so it was latent and never exercised by
the composition root, but nothing in the public contract forbids a future listener
from doing so.

Fixed by walking listener tokens via fresh `find()`/`upper_bound()` lookups each step
instead of holding a map iterator across a callback. A snapshot-copy of the listener
list was considered first and rejected because it would have broken the already-tested
behavior that a listener added mid-notification should still fire for
the in-flight delta (a snapshot taken before the loop starts can't see it). The
token-walk approach preserves that behavior for free: tokens are monotonically
increasing, so `upper_bound` naturally continues in registration order and picks up
anything added after the currently-firing listener's token.

## Everything else the review layer caught

- **Test-only interface mismatches** (blackbox-tester's sandbox couldn't read
  `AudioEngine.h`/`AudioRepository.h`/`Ranges.h`, so its hand-written fakes guessed at
  signatures): `float` vs `double` parameters, two missing `AudioRepository` pure
  virtuals, `shared_ptr<LoadedAudio>` vs the real `shared_ptr<const LoadedAudio>`. Fixed
  directly against the real headers rather than a respawn round-trip.
- **A degenerate test fixture**: `LoopPoints{}` (0, 0) was used as "a set loop" in
  several tests, but `ranges::clamp` legitimately nulls out any loop where
  `inSeconds >= outSeconds`. Not a bug, a test picking an accidentally-invalid value.
  Replaced with a real range.
- **An empty-`StateDelta{}` test bug**: one test suite used a default-constructed,
  all-fields-absent `StateDelta` as "a delta." `StateManager.applyDelta` drops empty
  deltas before they ever reach a listener, so the test could never have passed.
  Replaced with a delta carrying at least one field.
- **Five stale/wrong comments** (docs-adviser): a test comment describing the
  already-fixed range-for as current implementation; `MainComponent.h`'s top comment
  not mentioning the sync stack it already owns at M4; `TrackListComponent.h` still
  pointing at "M5" for routing that landed at M4; two test files referencing a
  `djapp::sync::` namespace that doesn't exist (flat `djapp`). All comment-only, fixed
  directly.

## Process note: what to stage for a blackbox-tester next time

The state-manager blackbox-tester's sandbox jail blocked it from reading anything
outside the staged spec, including the interfaces (`AudioEngine`, `AudioRepository`)
its own fakes needed to implement. The sync-nulltransport unit didn't have this
problem because its one dependency (`SyncTransport`) was small enough to paste in
full inside the spec text. When a unit's black-box tests need to implement a
third-party-shaped interface they don't own, stage that interface's full header text
into `.agent-scope/<unit>/` alongside the spec — a prose description isn't enough for
a sandboxed agent to get the signatures right.

## Unresolved risk

None outstanding. Every finding from the review layer was either fixed or resolved
directly, and the full suite is green (134/134, no skips).
