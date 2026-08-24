# M10 review fixes and cleanup

Closes out M10 (beat alignment): the two bugs the review layer found in the
sync button, a batch of five smaller findings, and the doc/config cleanup
left over from the design gate. Container green throughout: 360 client tests
(up from 356 after the sync fix's own regression coverage), clean build. No
server-side changes this milestone - server suite unchanged at 682/682. Host
checklist (`M10-host.md`) has not been run yet. `PROGRESS.md`'s M10 line says
so explicitly rather than implying sign-off that hasn't happened.

## The sync button's two bugs

Both found independently by `correctness-adviser` and `design-adviser` over
the full M10 diff, and re-derived by hand to confirm before fixing.

1. `computeBeatSync` wrapped both decks' phase into *this* deck's beat
   interval (`60 / thisGrid.bpm`), correct only when the two decks' BPMs
   happen to match - the sync button's entire reason to exist is the case
   where they don't. Fixed by wrapping each deck's phase in its own interval,
   then scaling the other deck's phase into this deck's interval by the
   `otherGrid.bpm / thisGrid.bpm` ratio before taking the shortest signed
   correction. Verified algebraically to reduce to the old formula exactly
   when the two BPMs are equal, so every pre-existing equal-BPM test case
   keeps passing unchanged - the fix is a strict generalization, not a
   rewrite.
2. The other deck's position came from `computeResumePositionSeconds`, a
   Play-button heuristic that substitutes 0.0 once that deck is stopped at
   track-end. Fine for its real purpose (resuming playback) but wrong as a
   phase reference. Fixed by giving `DeckComponent` two symmetrically-typed
   providers (`thisDeckSyncInfoProvider`, `otherDeckSyncInfoProvider`, both
   returning a new `DeckSyncInfo{beatGrid, positionSeconds, playbackRate}`)
   in place of the old asymmetric `beatGridProvider` /
   `std::pair<BeatGrid, double>` pair. `MainComponent` now reads each deck's
   live `AudioEngine::getCurrentPosition()` directly for sync, leaving
   `resumePositionProvider_` untouched for its original Play-button use.

Fixed together because they're coupled: the phase fix needs the other deck's
live `playbackRate` as a new input (so the tempo match targets what's
actually audible on that deck, not just its file's native BPM), and that
input had to come through the same provider redesign that fixed the position
bug.

**A third bug found during the fix, not by either adviser**: the new
`otherPlaybackRate` parameter was placed before the already-defaulted
`thisDurationSeconds` in `computeBeatSync`'s signature, kept default-argument
compiling for every existing call site as intended - except the one test that
called it with 5 positional arguments. That call's 5th argument silently
rebound to `otherPlaybackRate` instead of `thisDurationSeconds`, so the test
was no longer exercising the duration-clamp path it claimed to cover at all.
Caught during merge by noticing the test failure didn't match what the
semantic behavior change alone would produce. Fixed the call site and the
test's expected value in the same pass (the clamp-to-boundary behavior itself
was already changing - see below - so this assertion needed correcting
either way).

**Range-clamp fix, bundled into the same commit**: if the corrected nudge
would land outside `[0, thisDurationSeconds]`, position now falls back to
the unmodified current position instead of clamping to the boundary.
Clamping to the boundary landed on the last renderable frame and immediately
re-triggered the engine's own end-of-track self-stop.

New regression coverage: differing-BPM phase alignment (three concrete BPM
pairs, hand-derived expected values), non-1.0 `otherPlaybackRate` (confirms
it drives the tempo match but doesn't get double-counted in the phase step),
the corrected end-of-track fallback, and one equal-BPM sanity case guarding
the old behavior stayed intact.

## Five smaller findings, batched into one fix unit

All independent of each other and of the sync fix above, and small enough
individually not to warrant separate review passes:

- `EngineAdapter`'s beat-grid cache was keyed on a raw `const LoadedAudio*`
  that the class's own doc comment already admitted may dangle once
  `AudioRepository` releases the buffer. Reads `applied.trackId` instead,
  already available in `handleDelta` and stable regardless of the
  repository's caching behavior.
- `BufferPlaybackSource`'s pull-head reconstruction recomputed an
  approximation of the previous block's latency compensation using the
  *current* block's `rate`/`srcToDeviceRatio`, which mismatches whenever
  `rate_` changes between blocks (a pitch-slider or sync-button move).
  Caches the literal `laggedSourceFrames` value subtracted each block and
  adds back exactly that, not a recomputation.
- A zero-length render block (`numSamples == 0`) still pulled at least one
  frame, because the pull-frame calculation floors at 1. Added an early
  return before touching `pullAccumulator_`, `pos`, or the stretcher.
- The stretcher-reset threshold (200ms) was documented as excluding the sync
  button's phase nudge from resetting the stretcher, but the nudge's worst
  case (half a beat interval at a low BPM like 50-60) can reach ~0.6s -
  above the threshold. Raised to 1.0s. The stale claim in `DEVIATIONS.md`
  is corrected by a new dated entry rather than edited in place, keeping
  that file's history honest about what was believed when.
- `computeResumePositionSeconds`'s end-of-track epsilon (0.05s) was too
  narrow for a real stretcher's ~120ms+ output latency, so a track that
  self-stopped could resume from a stale near-end position instead of 0.
  Widened to 0.5s - the simplest fix consistent with the heuristic's own
  style, not a new latency-query API through `AudioEngine`.

## Doc and config cleanup

- Removed the unused `signalsmith_linear` `FetchContent` pin from
  `client/CMakeLists.txt`. Confirmed genuinely dead (never linked into
  anything) per the 2026-08-22 `DEVIATIONS.md` finding that Signalsmith
  Stretch v1.1.0 doesn't depend on it. Reverses that same entry's own
  "removing an already-reviewed pin isn't this unit's call to make
  unilaterally" reasoning - judged as the manager's call this time, on the
  strength of `CLAUDE.md`'s anti-speculative-generality stance, and recorded
  as such in `decisions.md`.
- Added qm-dsp and Signalsmith Stretch to `THIRD_PARTY_NOTICES.md`, full
  verbatim license text, mirroring the existing IXWebSocket entry's format.
  Neither had been added when M10 first vendored them.
- Added a message-thread-stall check to `M10-host.md`: `EngineAdapter::
  handleDelta` runs `BeatDetector::analyze()` synchronously, and a remote
  delta takes the same path as a local one, so loading a long track could in
  principle freeze either window's UI. This was deliberately deferred at
  design time pending real evidence. The checklist step now generates a
  3-minute click track (the existing 15-second tracks analyze too fast for a
  stall to be visible even if one exists) and checks both the controller and
  observer windows for a freeze. Not yet run.
- Recorded the sync button's deferred UX gaps in `DEVIATIONS.md`: no BPM
  readout, a silent no-op when detection fails on one side, and no visual
  distinction between "no beat grid," "already aligned," and "pressed but
  nothing happened." User call: fix in a future milestone, not by expanding
  M10's scope after sign-off.

## History reconciliation

Finalizing this session's work initially reset local history back to the
M10 design-gate stamp (`e952af1`), collapsing Units A-F's scaffold commits
along with this session's two fix waves into one uncommitted tree - the
orchestration skill's intended behavior. That surfaced a problem: `origin/
claude` already had those Units A-F commits pushed from an earlier session,
before this milestone's own finalize had ever run. Rewriting them now would
have required a force-push over already-published history.

User's call: preserve the pushed history. Reset local `HEAD` back to
`origin/claude`'s actual tip (the last already-pushed M10 commit) instead of
the design-gate stamp, so only this session's genuinely new work - both fix
waves plus the doc cleanup above - became new commits. Local stays a
fast-forward of `origin/claude`, and nothing already pushed was rewritten.

## Unresolved - logged as accepted residual risk, not fixed this session

Four low-severity findings from the same review layer, each judged not worth
its own fix unit:

- `BufferPlaybackSource::outputLatencyFrames()`'s naming doesn't match its
  actual contract as closely as it could.
- `attachBeatDetector` doesn't reconcile immediately on attachment the way
  `attachCrossfader` does (the crossfader pushes its current effective gain
  right away, the beat detector waits for the next track load).
- The `duration > 0.0 ? meta->durationSeconds : 0.0` pattern is repeated
  three times across the codebase instead of being pulled into one helper.
- `BufferPlaybackSource*Test.cpp`'s test helpers are duplicated across three
  test files instead of shared.

None of these affect correctness. All are candidates for a future
simplicity/docs pass rather than blockers.

## Read first, next session

`docs/plan/DEVIATIONS.md`'s two 2026-08-24 entries for the deferred sync UX
gaps and the message-thread-stall checklist step - both need the host
checklist run before they can be closed out. `docs/plan/PROGRESS.md`'s M10
line is written but not yet backed by a confirmed host pass.
