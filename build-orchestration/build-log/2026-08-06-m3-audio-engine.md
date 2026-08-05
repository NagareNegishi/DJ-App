# M3 — Single-deck audio engine

## What landed

`AudioEngine` (abstract interface), `BufferPlaybackSource` (the pure, offline-testable
DSP core), `JuceAudioEngine` (forwards the interface to one owned
`BufferPlaybackSource`), and `AudioDeviceHub` (owns the real JUCE audio device, one per
app) in `client/src/engine/`. A temporary dev UI on `MainComponent` (Load Selected,
Play/Pause, Seek, Gain, Rate) calls the engine directly, marked `// M4 replaces` for
deletion once `StateManager` lands. 87 tests in `client/tests/engine/`, `ctest` green. A
headless container run confirmed the no-device path stays up: `AudioDeviceHub` logs and
continues rather than crashing.

## Concurrency mechanisms decided at the design gate, before any code was written

The plan's threading section states intent (no locks, no allocation, no logging on the
audio thread) but not a mechanism. `design-adviser` surfaced five open questions against
the spec, resolved with the user before implementation:

- **Buffer handoff**: generation-counter epoch reclaim. The audio thread never touches a
  `shared_ptr`. It reads a raw `atomic<const LoadedAudio*>` and bumps a render-generation
  counter once per block. `load()` publishes the new pointer and sample rate as one
  indivisible unit, tags the old `shared_ptr` with the generation at swap time, and a
  lazy check on the next `load()` frees anything far enough behind the current
  generation.
- **Position ownership**: the audio thread is the sole writer of `positionSamples_`.
  `seek()`/`load()` hand off a pending value then a pending flag. The store order is
  load-bearing: the audio thread's acquire-load of the flag must never see it raised
  before the value it guards is visible.
- **End-of-track**: the engine self-stops with no notification path in M3, since
  `StateManager` doesn't exist yet to desync from. Noted for M4: `EngineAdapter` must
  poll `isPlaying()`, not expect a callback.
- **Loop points**: a double-buffered slot pair (`activeLoopSlot_` index flips after the
  inactive slot is fully written), so the audio thread never sees a torn in/out/active
  read.
- **Encapsulation**: all of the above is private. `JuceAudioEngine` only ever calls
  setter methods, each asserting `JUCE_ASSERT_MESSAGE_THREAD`.

Full reasoning: `build-orchestration/prompt-log/S2-adviser-1.md`,
`S2-unit-spec-m3-engine.md`.

## Boundary conditions resolved while writing the black-box suite

Before any render code existed, `blackbox-tester` hit three precision gaps translating
the spec into exact assertions, resolved as binding:

- Loop wrap is inclusive and same-block: if the head reaches or passes `outSeconds`
  mid-block, wrap immediately, preserving the fractional overshoot
  (`inSeconds + (oldPos - outSeconds)`, not a hard reset) so interpolation stays smooth
  across the seam.
- End-of-buffer stop is inclusive, same-block, and one sample short of the array end
  (`pos >= numSourceFrames - 1`), because linear interpolation reads
  `src[index0 + 1]`, so the last renderable position is `numSourceFrames - 2`.
- Defaults: `gain = 1.0f`, `playbackRate = 1.0f`, matching `PlaybackState`'s own
  defaults.

## What the white-box pass and review layer caught, and what happened to it

- **`load(nullptr)` mid-playback left `isPlaying()` stuck `true`** (white-box finding).
  The early-return-on-no-buffer path cleared output but skipped the later code that
  flips `playing_` false. Fixed by checking buffer validity independently of the
  play/pause check, so "no track loaded" now also means "not playing," the intuitive
  contract. One test updated to assert the corrected behavior.
- **Loop retention across `load()`** (correctness-adviser, out-of-scope/decide). Loop
  in/out points are stored as sample counts converted at `setLoop()` time. Loading a
  different track at a different sample rate left those counts denominated in the wrong
  rate, wrapping at the wrong position. Decided with the user: `load()` now clears the
  loop, same as a fresh `BufferPlaybackSource`. Regression test added via a second
  black-box pass, scoped to just this one behavior.
- **Per-sample pointer re-resolution in the render loop** (performance-adviser,
  medium). `getReadPointer`/`setSample` were called once per sample per channel, up to
  ~1024 redundant resolutions per block, instead of once per block. Hoisted into a
  fixed-size local cache read and written directly inside the sample loop, a pure hoist
  with no behavior change.
- **Five documentation gaps** (docs-adviser): the self-stop/poll contract, the
  pending-seek store-order invariant, the `numSourceFrames - 1` boundary, the loop-wrap
  overshoot formula, and degenerate-loop-point handling were all real mechanisms with no
  comment explaining the why. Each was surfaced to the user individually (docs-adviser
  split them `fix` vs `decide` by how much judgment the placement needed). All resolved
  to "add the comment," landed in the same fix batch as the two items above.

All four review axes (correctness, performance, docs, plus the design gate before code
existed) ended clean: `ctest` 87/87 after the fix batch, no unresolved findings.

## Process note: a test-authoring bug surfaced by the manager, not a suite failure

The black-box tester's regression test for the `load()`-clears-loop fix initially failed
after landing. It loaded a second, differently-rated track expecting the loop clear to be
the only thing under test, but the sample-rate mismatch changed the render increment
enough that the source ran out and self-stopped mid-check. That's a real, correct engine
behavior, but the test's own wrap-detection heuristic misread it as evidence the stale
loop had survived. Diagnosed by walking through the render-increment arithmetic
(`rate × sourceSampleRate / deviceSampleRate`) rather than reflexively suspecting the
engine change. The test was corrected (matching sample rates, a longer second track) and
passed. A new test failing right after a fix lands isn't automatic evidence the fix is
wrong. Check whether the test's own setup holds first.

## Unresolved risk

None outstanding. Every finding from the review layer was either fixed or resolved with
the user, and both suites are green.
