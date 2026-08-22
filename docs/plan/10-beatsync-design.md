# 10 — Beat alignment design (M10)

*Design-gate document per `07-milestones.md`'s M10 entry. No code lands until this is signed off. Concrete C++ signatures here are proposals for review, not final — they follow the existing interface style (`AudioEngine.h`, `EngineAdapter.h`) but implementers may adjust names during the build.*

## Goal

Tempo-match and phase-align deck B to deck A (or vice versa) on a button press. Two new capabilities behind interfaces, per `docs/stack.md`'s deferred-library table: `TimeStretcher` (change speed without changing pitch) and `BeatDetector` (find a track's BPM and beat phase from its decoded audio).

## Scope decision: no protocol change

The sync action only ever produces values in fields that already exist and already sync: `playbackRate` and `positionSeconds` (both already in `PlaybackState`/`StateDelta`, `02-protocol.md`). `pitchOffsetSemitones` also already exists — M10 makes it audible, it doesn't add it. Beat detection results (BPM, beat phase) are **not synced**: every client analyzes its own local copy of the track file with the same deterministic algorithm and gets the same answer, matching the app's existing principle that computation is local and only state crosses the network (`01-architecture.md` core principle 1, and `decisions.md` §2.2's same reasoning for position). So: **no protocol version bump, no fixture changes** for M10 — unlike M8 (crossfader) and M9 (repeat), which both needed one.

The one edge case this creates: two users with byte-different copies of "the same" track (different encode, padding, silence) could get slightly different beat-phase results locally. This is the same risk class already accepted for missing/mismatched tracks (M7 checklist), not a new one — noted, not solved.

## `TimeStretcher`

Backed by **Signalsmith Stretch** (verified against its actual header, not paraphrased docs — see Library choices below). One real API shape difference from a naive design: Signalsmith Stretch has no `setTempo()`. The stretch ratio isn't a settable field at all — it's implied purely by how many input frames vs. output frames you pass to a single `process()` call in the same block.

```cpp
// engine/TimeStretcher.h
class TimeStretcher
{
  public:
    virtual ~TimeStretcher() = default;
    // Called once from load()/prepareToPlay() (message thread; may allocate/reserve).
    virtual void prepare(int numChannels, double sampleRate) = 0;
    // load()/seek() call this: flush internal state so a stretched boundary doesn't glitch.
    virtual void reset() = 0;
    virtual void setPitchSemitones(float semitones) = 0; // independent of tempo; range matches pitchOffsetSemitones [-12, 12]
    // Tempo ratio is expressed by numInput vs numOutput, not a settable field: feed numInput
    // source-rate frames, request numOutput stretched frames — caller computes numInput per block
    // from the current rate_ (numInput ≈ numOutput × rate_). Caller-owned buffers, no allocation
    // inside this call once prepare() has run (see "RT-safety" below for how firmly that's held).
    virtual void process(const float* const* inChannels, int numInput, float* const* outChannels, int numOutput) = 0;
};
```

Two implementations: `IdentityTimeStretcher` (passthrough — today's exact naive-resample behavior, kept for tests and as the always-available fallback) and `SignalsmithTimeStretcher`, which maps 1:1 onto the real library: `prepare` → `presetDefault(numChannels, sampleRate)`, `setPitchSemitones` → `setTransposeSemitones(semitones)`, `reset` → `reset()`, `process` → `process(inputs, numInput, outputs, numOutput)` verbatim (all four confirmed present with these exact signatures in `signalsmith-stretch.h`).

### Where it sits in `BufferPlaybackSource`

Today, `getNextAudioBlock` does per-sample linear interpolation directly against `rate_`, which is why rate changes pitch (`02-protocol.md`: "prototype: rate affects pitch (no timestretch)"). A real time-stretcher doesn't fit that per-sample loop: it consumes input and produces output at a non-fixed ratio, buffered internally via STFT, so the sample-by-sample `index0`/`index1`/`frac` render loop becomes a two-stage pipeline instead: for each block, compute `numInput = round(numOutput × rate_)` source-rate frames needed, pull that many frames from the source buffer (existing loop-wrap/repeat-wrap boundary logic, `wrapPositionWithinRange`, moves to this pull stage, operating on the *source* position same as today), then call `process()` once to fill the block's `numOutput` stretched frames.

`pitchOffsetSemitones` becomes a new atomic (message-thread-written, same pattern as `rate_`/`gain_`) driving `setPitchSemitones`, independent of the tempo computation above.

**Startup latency, flagged as an implementation detail, not a blocking design question:** an STFT-based stretcher has inherent input/output latency (`inputLatency()`/`outputLatency()`, both confirmed present in the header) — it can't produce the first output frame until it's seen enough input. For a track that just started playing, this means either a brief silence at play-start equal to `outputLatency()` frames, or a pre-roll (feed silent/priming input at `load()` time, before the user presses play, to drain that latency ahead of time). Recommend pre-roll at `load()` — cheap, keeps `play()` itself glitch-free — but this is a build-time detail, not something that needs sign-off now; noting it here so it isn't lost, and it should be confirmed audibly by the M10 host checklist regardless of which way it's implemented.

### RT-safety: resolved, with a required verification step

`01-architecture.md`'s binding rule: the audio thread takes no locks and allocates nothing, ever. Signalsmith Stretch's dependency, Signalsmith Linear, documents a real pre-sizing API for exactly this: `template<typename V> void reserve(size_t size)` (confirmed present in `linear.h`) — call it once during `prepare()` with the longest chunk length the deck will ever request, and steady-state `process()` calls afterward shouldn't need to grow anything.

Independent evidence this holds up in real deployment, not just the library's own claim: **OpenMPT** (a 25+ year, technically rigorous open-source tracker) replaced SoundTouch with Signalsmith Stretch as its default time-stretch/pitch-shift engine in its 1.32 release, specifically because SoundTouch's time-domain approach produced audible artifacts — a direct, shipped, independent head-to-head Signalsmith won ([OpenMPT 1.32 release notes](https://openmpt.org/release_notes/OMPT_1.32_ReleaseNotes.html)). Developer feedback on KVR Audio (a DSP/plugin-dev forum) independently rates its quality at or above Rubber Band, the higher-quality GPL alternative, at a fraction of the CPU cost.

What's *not* independently verified: whether `reserve()`'s no-growth guarantee holds specifically inside a hard-real-time audio callback the way this app calls it — that's the library's documented contract, not something a third party has stress-tested in public. There's also a genuine ambiguity worth resolving in code, not in this doc: `linear.h` has both a `LinearImplBase` and a `LinearImpl` type, and it wasn't confirmed which one's `reserve()` is the real implementation vs. a possible no-op template on the base — the public `Linear` alias does resolve to the real `LinearImpl<true>`, but this should be double-checked when the actual dependency is vendored in.

**Required implementation-time task, not optional polish:** before this ships, add a test (allocation-interposing allocator, or an ASan/valgrind-style hook around `process()`) confirming zero allocations after `prepare()`/`reserve()` have run, at the app's actual block sizes. If that test fails, fall back to Option B (worker-thread + lock-free ring buffer ahead of the playhead) rather than shipping an unverified violation of the no-allocation rule. Record the outcome (pass, or the fallback taken) in `docs/decisions.md` under M10 either way.

### Test impact

The existing offline-render suite (`05-testing.md`: "rate 2.0 consumes the buffer in half the blocks") currently pins the *naive* resample behavior — once `TimeStretcher` lands, rate 2.0 no longer means "consume twice as fast with pitch shifted," it means "twice as fast, pitch preserved unless `pitchOffsetSemitones` says otherwise." That test needs updating to assert against `IdentityTimeStretcher` (preserves today's exact behavior, so existing assertions stay valid there) plus new tests against the real stretcher's contract (tempo changes speed only, pitch changes pitch only, independently). Belongs in the implementation milestone's task list, not this doc — noted here so it isn't lost.

## `BeatDetector`

```cpp
// engine/BeatDetector.h
struct BeatGrid
{
    double bpm = 0;              // 0 = detection failed / silence / too short; derived, not a library output (see below)
    double firstBeatSeconds = 0; // position of the first detected beat, seconds from track start
};

class BeatDetector
{
  public:
    virtual ~BeatDetector() = default;
    virtual BeatGrid analyze(const LoadedAudio& audio) = 0; // whole-buffer, offline, message-thread caller
};
```

### Chosen implementation: `QmDspBeatDetector`, wrapping qm-dsp

Backed by Queen Mary University of London's `qm-dsp` (`github.com/c4dm/qm-dsp`), verified against its actual source — the same real DJ-software precedent Mixxx uses for this exact job (Mixxx's `AnalyzerQueenMaryBeats` calls the identical class pair below), confirmed by reading Mixxx's analyzer source directly, not by assumption.

Two qm-dsp classes, not one: `DetectionFunction` (`dsp/onsets/DetectionFunction.h`) is fed one analysis window at a time — `analyze()` loops it across the whole decoded buffer, accumulating one detection-function value per window into a vector, the same "loop windows, accumulate a vector" shape as any windowed offline analysis. Once the whole track's detection function is built, a single batch call to `TempoTrackV2::calculateBeatPeriod(...)` then `calculateBeats(...)` (`dsp/tempotracking/TempoTrackV2.h`) turns that vector into an array of beat-frame indices in one shot.

That array is the real output — **not a single BPM value**. qm-dsp's beat tracker returns where every beat lands, which is a better fit for our needs than a bare scalar: `firstBeatSeconds` is directly `beats[0]` converted from frame index to seconds, and `bpm` is derived ourselves from the beat grid — `60 / median(inter-beat-interval in seconds)` — which is also more robust than a single global autocorrelation peak, since it comes from the actual detected grid rather than one estimate. This derivation is our own code, a few lines, not a qm-dsp API call.

Dependency footprint for exactly this path is minimal and self-contained, confirmed by reading the actual includes and build flags: `DetectionFunction`/`TempoTrackV2` need only qm-dsp's own FFT wrapper (backed by the bundled `kissfft`, BSD-3) — no FFTW, no Vamp SDK, no Boost (Boost is qm-dsp's test-only dependency). The exact minimal `.cpp` file set (expected: `DetectionFunction.cpp`, `TempoTrackV2.cpp`, the FFT/PhaseVocoder wrapper, and the bundled `kissfft` sources) needs confirming against the real `#include` chain at implementation time — not asserted as final here, since qm-dsp ships no CMake target of its own to consult (Makefile/MSVC-project only; we hand-write a small CMake target, the same thing Mixxx itself does).

**Pin and license, to be recorded in `docs/decisions.md` alongside this design:**
- qm-dsp has no tagged release for the DSP core (only two old tags belonging to a sibling Vamp-plugin project). Pin an exact commit SHA on `c4dm/qm-dsp` `master` (select the specific SHA at implementation time; document it as a deviation from the project's "pin to a tag" convention, same shape as any other untagged-upstream dependency) — this is a small, academically mature, slow-moving DSP library, not an actively-churning platform, so a SHA pin here is a materially safer bet than the same deviation would have been for Essentia.
- License is GPL-2.0-or-later (confirmed via qm-dsp's own `COPYING`), with a few internal files separately permissive (bundled `kissfft` BSD-3, one allocator header MIT-style) that don't weaken the governing GPL. This is the `CLAUDE.md`-mandated "GPL beyond JUCE needs a recorded decision" case — the decision is: proceed, on the strength of the Mixxx production precedent and the lean, self-contained dependency footprint for the specific classes used.

**Correction to the plan's own stated assumption**, found during this research and worth recording: `docs/stack.md` and `07-milestones.md` both describe aubio as "the de facto open-source choice, used by Mixxx." That's stale — Mixxx does not use aubio; it uses qm-dsp (primary) and SoundTouch's `BPMDetect` (legacy fallback, BPM-only, no beat-phase output). aubio itself has had no tagged release since 2019 and is waf-built (real integration friction); it was dropped from consideration on that basis.

### Documented future swap: hand-rolled autocorrelation detector

`BeatDetector` is an interface specifically so this doesn't need to be the permanent answer. If the GPL dependency or the untagged-commit pin ever becomes a real maintenance cost, the intended replacement is a hand-rolled autocorrelation-based detector — same algorithm family qm-dsp itself and SoundTouch's `BPMDetect` both use (onset-strength envelope → autocorrelation for tempo → peak-picking for phase), a well-established, non-research-grade technique (see librosa's documented `beat_track` pipeline, citing Ellis 2007). Swapping the concrete implementation behind `BeatDetector` is a contained change — nothing else in the codebase touches which implementation is wired in, the same pattern already used for `IdentityTimeStretcher` vs `SignalsmithTimeStretcher`.

### Threading and caching

Runs once per track load, alongside `AudioRepository::getAudioBuffer` — which, despite the aspirational "(async)" comment in `AudioRepository.h`, is currently synchronous on the message thread (blocking decode). M10 keeps that same posture for beat analysis rather than introducing new async infrastructure: synchronous, on load, accepted latency. This is a "decide by effort" call in the M8/M9 style — flagging it rather than silently picking it, since a slow analysis on a long track would be a visible load-time stall. If the host checklist finds it's actually a problem, moving it off the message thread is a contained follow-up, not a redesign (same shape as the repository's own noted async gap).

`TrackMetadata::bpm` (manifest-declared, currently unused beyond storage) stays purely informational/display — the sync button always uses `BeatDetector`'s live analysis of the loaded buffer, not the manifest value, so there's no reconciliation logic needed between the two possibly-disagreeing numbers.

Result is cached per loaded buffer (deck-keyed, next to where `EngineAdapter` already resolves `trackId` → buffer) so reselecting an already-loaded track doesn't re-analyze.

### Test impact

Unit tests should feed synthetic click-track buffers (regular impulses at a known BPM, same "small synthetic buffers" testing style `05-testing.md` already uses for `BufferPlaybackSource`) through the real `QmDspBeatDetector` and assert the recovered BPM and beat spacing land within tolerance — a meaningful correctness test, not just a build check. A trivial `NullBeatDetector` (always returns `BeatGrid{}`) covers tests that exercise the sync-button/`EngineAdapter` wiring without needing real detection.

## Library choices

Per `docs/stack.md`'s decision triggers, both fire now that beat alignment work begins.

**Time-stretch: Signalsmith Stretch — overrides `docs/stack.md`'s SoundTouch default.** `docs/stack.md` named SoundTouch as the placeholder default back when this was a future concern; this design gate is exactly where that gets revisited, and it doesn't hold up against the alternative surfaced here. Signalsmith Stretch (MIT) and its one dependency Signalsmith Linear (MIT) are both fully permissive — no LGPL dynamic-link obligation to manage, no `CLAUDE.md` recorded-decision overhead at all — and header-only, so integration is plain `FetchContent` with no build-system risk (contrast the waf/CMake friction flagged for aubio in the `BeatDetector` section above). On top of the license and integration wins, it's the better dependency on quality/reliability grounds too: see the OpenMPT/KVR evidence in the RT-safety section above. This substitution should be recorded in `docs/decisions.md` under M10 as a design-gate override of the stack doc's original default.

Pin both explicitly, matching this project's "pin everything, never a floating branch" convention (`06-security.md`, same pattern as IXWebSocket's tag pin):
- `signalsmith-stretch` @ tag `1.1.0` (commit `44c8f865af9da8c29cc4a70a2d5a3ec83639c711`) — its own `main` has moved past this with no newer tag cut, so `1.1.0` is the latest stable point to pin to.
- `signalsmith-linear` @ tag `0.6.0` (commit `8be69c57b7064822076c2cfc55a522e5f5867cc1`) — pinned **explicitly and separately** in our `CMakeLists.txt`, not left to whatever `signalsmith-stretch`'s own `CMakeLists.txt` transitively fetches (its default is the much older `0.3.1`).

**Beat detection: qm-dsp — overrides `docs/stack.md`'s aubio default.** Full rationale, dependency/pin details, and the documented future swap are in the `BeatDetector` section above; summary here for parallel structure with the time-stretch pin above. `docs/stack.md` and `07-milestones.md` named aubio as the default, describing it as "used by Mixxx" — verified stale (Mixxx uses qm-dsp, not aubio; see above), and aubio is separately disqualified by having no tagged release since 2019 and a `waf` build.

Pin: `c4dm/qm-dsp` at an exact commit SHA on `master` (no tagged release exists for the DSP core — select the specific SHA at implementation time, recorded as a documented deviation from the tag-pinning convention, same as noted above). License: GPL-2.0-or-later, recorded in `docs/decisions.md` §6.1 per `CLAUDE.md`'s GPL rule.

## Sync-button semantics

Each deck gets a sync button (`DeckComponent`, icon-based like M9's repeat toggle — reuse the `juce::DrawableButton`/`juce::Path` pattern from `makeRepeatGlyph()`, gated through `deckControlEnabled` like every other deck widget). Pressing deck X's sync button matches **X to the other deck**; the other deck is the reference and is left untouched. This is the standard DJ-software convention (Serato/Traktor: you press sync on the deck you want to change).

One-shot, not continuous: sync computes a correction and applies it once on press — no ongoing beat-lock loop. This matches the project's already-accepted stance on position drift (`decisions.md` §2.2: correction happens at discrete moments, drift between them is accepted), and keeps the milestone bounded. If continuous lock is wanted later, it's an additive follow-up, not a redesign.

**Step 1 — match BPM.** `newPlaybackRate = otherDeck.beatGrid.bpm / thisDeck.beatGrid.bpm`, clamped to `[playbackRateMin, playbackRateMax]` (`Ranges.h`, unchanged — `0.5..2.0`). If the true ratio falls outside that range, sync is only partial and the residual mismatch is accepted, same clamp-and-accept posture `Ranges.h` already applies everywhere else.

**Step 2 — nudge phase.** Beats occur at `firstBeatSeconds + k × (60/bpm)` for integer `k`. At the moment of the button press, find the phase offset between the two decks' beat grids and apply a single corrective seek to `thisDeck` — never a full re-seek to the other track's absolute position, only a shift by at most half a beat interval (`±(60/bpm)/2` seconds) to land on the nearest beat. Implemented as one `requestSeek` call through the existing `AudioEngine` interface — no new engine method needed.

Both steps produce one `StateDelta` (deck X, `playbackRate` + `positionSeconds` both set) applied through the existing `StateManager.applyDelta` path — `StateDelta` already supports setting independent optional fields together, so no new delta shape is needed either.

## `pitchOffsetSemitones`, finally rendered

Wired straight to `TimeStretcher::setPitchSemitones`, independent of tempo. M10 also exposes it in the UI for the first time (`02-protocol.md`: "UI does not expose it before then") — a slider on `DeckComponent` alongside gain/rate, same enablement gating (`deckControlEnabled`), same clamp range already in `Ranges.h` (`[-12, 12]`). No protocol change; the field and its range have existed since M1.

## Open decisions requiring sign-off

1. ~~Real-time-safety posture for `TimeStretcher::process()`~~ — **resolved**: Signalsmith Stretch, Option A (call directly from the audio thread, backed by `Linear::reserve()`'s documented pre-sizing contract plus OpenMPT/KVR production evidence), gated on a required implementation-time allocation test with Option B as the named fallback if that test fails.
2. ~~Beat detection library~~ — **resolved**: qm-dsp (`QmDspBeatDetector`) now — same classes Mixxx uses, verified minimal dependency footprint, GPL-2.0-or-later accepted and recorded — with a hand-rolled autocorrelation detector documented as the intended future swap behind the same `BeatDetector` interface if the GPL dependency or untagged-commit pin ever becomes a real cost.
3. Everything else above (no protocol bump, sync-button one-shot/per-deck semantics, synchronous on-load analysis, Signalsmith Stretch + Linear pinned versions, qm-dsp pin/license, the STFT-latency pre-roll note) is a design call already made in this doc.

**All open decisions resolved — this design is ready for sign-off.**
