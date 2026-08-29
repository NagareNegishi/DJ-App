# Unit G — reject a spurious BPM when a track has no real beat

Part of DJ App milestone M10 (beat alignment), a fix unit against Unit B's
already-landed `QmDspBeatDetector` (`docs/plan/10-beatsync-design.md`
section "`BeatDetector`"). Found live on the M10 host checklist
(`docs/plan/checklists/M10-host.md`, Step 11) on 2026-08-30; full writeup in
`docs/plan/DEVIATIONS.md`'s "2026-08-30 - the sync button doesn't no-op when
one deck has no detectable beat" entry (read it first — this spec summarizes
it but that entry is the authoritative record of what was actually
observed).

## Goal

`model/BeatSync.h::computeBeatSync` and the M10 host checklist both document
"beat detection failed" as `BeatGrid.bpm <= 0` (`std::nullopt`, no
correction, a silent no-op on the Sync button). `QmDspBeatDetector::analyze`
(`client/src/engine/QmDspBeatDetector.cpp`) currently only reaches that
`BeatGrid{}` sentinel through one guard — `beats.size() < 2` — which correctly
catches true silence and too-short buffers (the only two failure cases
`client/tests/engine/BeatDetectorTest.cpp` exercises today) but not a
sustained tone with continuous nonzero energy and no real onsets: qm-dsp's
`TempoTrackV2` is a Viterbi/dynamic-programming tempo tracker with no
internal "no real periodicity, give up" branch, so it always emits *some*
best-guess periodic beat sequence once fed enough detection-function frames,
real or not. Reproduced on the host: a plain sine tone (`demo1.wav`) fed into
`analyze()` returned a real-looking but meaningless `bpm`, which
`computeBeatSync` then happily used, producing an audible, wrong rate jump on
Sync instead of a no-op.

Add a plausibility gate before the tempo tracker ever runs: if the built
`detectionValues` vector shows no meaningful onset-energy variation (i.e. the
signal has no real transients for `DetectionFunction` to have latched onto),
treat it the same as the existing `beats.size() < 2` case and return
`BeatGrid{}` without calling `TempoTrackV2::calculateBeatPeriod`/
`calculateBeats` at all.

## Files to modify

- `client/src/engine/QmDspBeatDetector.cpp` — add the gate inside `analyze()`,
  after `detectionValues` is built (right before the existing
  `TempoTrackV2 tempoTracker(...)` construction), same function, no interface
  or header change.
- `client/tests/engine/BeatDetectorTest.cpp` — add a regression test using a
  synthetic sustained-tone buffer (steady-amplitude sine, no clicks — the
  existing `makeClickTrack`/`makeSilence` helpers in that file cover
  "rhythmic" and "literal silence"; this needs a third helper for "continuous
  but arrhythmic," e.g. `makeSteadyTone(sampleRate, durationSeconds,
  frequencyHz)` filling every sample with a sine wave and no silence at all).
  Assert `grid.bpm == 0.0` and `grid.firstBeatSeconds == 0.0`, matching the
  existing silence/too-short assertions' shape.

## Implementation guidance

The exact statistic and threshold are your call to tune empirically against
both the new sustained-tone test and the six existing `BeatDetectorTest.cpp`
cases (all of which must keep passing, tolerances unchanged) — this is not
prescribed further than the shape below because it depends on qm-dsp's actual
`DF_COMPLEXSD` output range, which is best measured, not guessed:

- A flat/near-constant `detectionValues` (a steady tone) should look very
  different in aggregate from a click track's sharp, sparse spikes against a
  near-zero baseline. A relative measure (e.g. coefficient of variation —
  standard deviation over mean — or the ratio of peak to median) is more
  robust than an absolute threshold, since `DetectionFunction`'s output scale
  depends on input amplitude, not just its rhythmic content.
- Compute the statistic once, right after the `detectionValues` loop; if it
  falls below whatever threshold you settle on, `return BeatGrid{};`
  immediately — skip `TempoTrackV2` entirely, don't let it run and then
  discard the result, since the whole point is to never trust a tempo guess
  built from a curve with no real onsets in it.
- If a coefficient-of-variation-style gate turns out to also reject a
  legitimate low-dynamic-range click track from the existing suite, adjust
  the statistic or threshold rather than special-casing — the six existing
  tests are the floor this must not regress.

## Constraints

- Do not touch `model/BeatSync.h`, `BeatDetector.h`, `NullBeatDetector.h`, or
  any engine/UI call site — the fix is entirely inside
  `QmDspBeatDetector::analyze`'s existing failure-signaling contract
  (`bpm <= 0`), which every caller already handles correctly. This is a
  narrower detection of an existing documented case, not a new one.
- Keep the fix confined to `QmDspBeatDetector.cpp`'s anonymous namespace /
  `analyze()` body — no new public API surface.
- Build + full suite: `cmake -S client -B client/build/linux -G Ninja &&
  cmake --build client/build/linux && ctest --test-dir client/build/linux
  --output-on-failure` must pass, including the six pre-existing
  `BeatDetectorTest.cpp` cases and the new one.

## After this unit lands

Not part of this unit's own scope, but the reason it exists: the M10 host
checklist (`docs/plan/checklists/M10-host.md`) was paused mid-run at Step 11
because of this bug (Steps 1-10 passed; two things to carry into that
checklist's closing report regardless — Step 8's constant, non-growing
phase-offset artifact right after a sync press, and this Step 11 failure).
Once this unit is merged and green in the container, the user re-pulls and
rebuilds on the Windows host and re-runs Step 11 (retest only — Steps 1-10
don't need repeating) before the checklist continues into the multi-user
section (Steps 12-17).

## Report back

End your final message with a report in the implementer format (Build:
pass/fail; files changed; the statistic/threshold chosen and why; confirmation
the six pre-existing `BeatDetectorTest.cpp` cases still pass unchanged; any
`Open` question).
