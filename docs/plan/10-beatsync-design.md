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

## `BeatDetector`

```cpp
// engine/BeatDetector.h
struct BeatGrid
{
    double bpm = 0;              // 0 = detection failed / silence / too short
    double firstBeatSeconds = 0; // position of the first detected beat, seconds from track start
};

class BeatDetector
{
  public:
    virtual ~BeatDetector() = default;
    virtual BeatGrid analyze(const LoadedAudio& audio) = 0; // whole-buffer, offline, message-thread caller
};
```

Runs once per track load, alongside `AudioRepository::getAudioBuffer` — which, despite the aspirational "(async)" comment in `AudioRepository.h`, is currently synchronous on the message thread (blocking decode). M10 keeps that same posture for beat analysis rather than introducing new async infrastructure: synchronous, on load, accepted latency. This is a "decide by effort" call in the M8/M9 style — flagging it rather than silently picking it, since a slow analysis on a long track would be a visible load-time stall. If the host checklist finds it's actually a problem, moving it off the message thread is a contained follow-up, not a redesign (same shape as the repository's own noted async gap).

`TrackMetadata::bpm` (manifest-declared, currently unused beyond storage) stays purely informational/display — the sync button always uses `BeatDetector`'s live analysis of the loaded buffer, not the manifest value, so there's no reconciliation logic needed between the two possibly-disagreeing numbers.

Result is cached per loaded buffer (deck-keyed, next to where `EngineAdapter` already resolves `trackId` → buffer) so reselecting an already-loaded track doesn't re-analyze.

## Library choices

Per `docs/stack.md`'s decision triggers, both fire now that beat alignment work begins.

**Time-stretch: Signalsmith Stretch — overrides `docs/stack.md`'s SoundTouch default.** `docs/stack.md` named SoundTouch as the placeholder default back when this was a future concern; this design gate is exactly where that gets revisited, and it doesn't hold up against the alternative surfaced here. Signalsmith Stretch (MIT) and its one dependency Signalsmith Linear (MIT) are both fully permissive — no LGPL dynamic-link obligation to manage, no `CLAUDE.md` recorded-decision overhead at all — and header-only, so integration is plain `FetchContent` with no build-system risk (contrast the waf/CMake friction flagged for aubio below). On top of the license and integration wins, it's the better dependency on quality/reliability grounds too: see the OpenMPT/KVR evidence in the RT-safety section above. This substitution should be recorded in `docs/decisions.md` under M10 as a design-gate override of the stack doc's original default.

Pin both explicitly, matching this project's "pin everything, never a floating branch" convention (`06-security.md`, same pattern as IXWebSocket's tag pin):
- `signalsmith-stretch` @ tag `1.1.0` (commit `44c8f865af9da8c29cc4a70a2d5a3ec83639c711`) — its own `main` has moved past this with no newer tag cut, so `1.1.0` is the latest stable point to pin to.
- `signalsmith-linear` @ tag `0.6.0` (commit `8be69c57b7064822076c2cfc55a522e5f5867cc1`) — pinned **explicitly and separately** in our `CMakeLists.txt`, not left to whatever `signalsmith-stretch`'s own `CMakeLists.txt` transitively fetches (its default is the much older `0.3.1`).

**Beat detection: still needs your decision — unchanged by this update.** The milestone's stated default is aubio, but it's GPL — `CLAUDE.md`'s rule ("anything GPL beyond JUCE needs a recorded decision first") and `06-security.md` both require this recorded before integrating, not assumed. Two real options:

| Option | License fit | Integration cost | Accuracy |
|---|---|---|---|
| **aubio** | GPLv3. Combining with this project's AGPL-3.0 client is legally sound (GPLv3 §13 grants explicit permission for combination with AGPL-licensed work; the combined work as distributed must be treated as AGPL) — but it *is* a copyleft dependency this project didn't have before, and needs the recorded decision either way. | Real friction: aubio's native build is `waf`, not CMake — FetchContent won't "just work" like SoundTouch/IXWebSocket did; likely needs a hand-written CMake wrapper around its sources or a vendored prebuilt. | Purpose-built beat/onset detector, better accuracy, used by Mixxx |
| **Hand-rolled minimal detector** (e.g. autocorrelation over an onset-strength envelope) | No new dependency, no license question at all | Some implementation effort, but it's ours — no foreign build system to integrate | Lower accuracy than aubio, but for "match BPM + nudge to nearest beat" between two tracks a DJ already chose as compatible, autocorrelation BPM estimation is a well-understood, tractable amount of DSP |

I lean toward the hand-rolled detector: it sidesteps both the GPL recorded-decision overhead and the waf/CMake integration risk in one move, and the accuracy bar for "nudge phase to the nearest beat" is lower than full beat-tracking software needs. But this is exactly the kind of call the milestone text flags as needing your sign-off, not mine to make silently.

## Sync-button semantics

Each deck gets a sync button (`DeckComponent`, icon-based like M9's repeat toggle — reuse the `juce::DrawableButton`/`juce::Path` pattern from `makeRepeatGlyph()`, gated through `deckControlEnabled` like every other deck widget). Pressing deck X's sync button matches **X to the other deck**; the other deck is the reference and is left untouched. This is the standard DJ-software convention (Serato/Traktor: you press sync on the deck you want to change).

One-shot, not continuous: sync computes a correction and applies it once on press — no ongoing beat-lock loop. This matches the project's already-accepted stance on position drift (`decisions.md` §2.2: correction happens at discrete moments, drift between them is accepted), and keeps the milestone bounded. If continuous lock is wanted later, it's an additive follow-up, not a redesign.

**Step 1 — match BPM.** `newPlaybackRate = otherDeck.beatGrid.bpm / thisDeck.beatGrid.bpm`, clamped to `[playbackRateMin, playbackRateMax]` (`Ranges.h`, unchanged — `0.5..2.0`). If the true ratio falls outside that range, sync is only partial and the residual mismatch is accepted, same clamp-and-accept posture `Ranges.h` already applies everywhere else.

**Step 2 — nudge phase.** Beats occur at `firstBeatSeconds + k × (60/bpm)` for integer `k`. At the moment of the button press, find the phase offset between the two decks' beat grids and apply a single corrective seek to `thisDeck` — never a full re-seek to the other track's absolute position, only a shift by at most half a beat interval (`±(60/bpm)/2` seconds) to land on the nearest beat. Implemented as one `requestSeek` call through the existing `AudioEngine` interface — no new engine method needed.

Both steps produce one `StateDelta` (deck X, `playbackRate` + `positionSeconds` both set) applied through the existing `StateManager.applyDelta` path — `StateDelta` already supports setting independent optional fields together, so no new delta shape is needed either.

## `pitchOffsetSemitones`, finally rendered

Wired straight to `TimeStretcher::setPitchSemitones`, independent of tempo. M10 also exposes it in the UI for the first time (`02-protocol.md`: "UI does not expose it before then") — a slider on `DeckComponent` alongside gain/rate, same enablement gating (`deckControlEnabled`), same clamp range already in `Ranges.h` (`[-12, 12]`). No protocol change; the field and its range have existed since M1.

## Test impact (flagging, not deciding)

The existing offline-render suite (`05-testing.md`: "rate 2.0 consumes the buffer in half the blocks") currently pins the *naive* resample behavior — once `TimeStretcher` lands, rate 2.0 no longer means "consume twice as fast with pitch shifted," it means "twice as fast, pitch preserved unless `pitchOffsetSemitones` says otherwise." That test needs updating to assert against `IdentityTimeStretcher` (preserves today's exact behavior, so existing assertions stay valid there) plus new tests against the real stretcher's contract (tempo changes speed only, pitch changes pitch only, independently). Belongs in the implementation milestone's task list, not this doc — noted here so it isn't lost.

## Open decisions requiring sign-off

1. ~~Real-time-safety posture for `TimeStretcher::process()`~~ — **resolved**: Signalsmith Stretch, Option A (call directly from the audio thread, backed by `Linear::reserve()`'s documented pre-sizing contract plus OpenMPT/KVR production evidence), gated on a required implementation-time allocation test with Option B as the named fallback if that test fails.
2. **Beat detection library — still open.** aubio (GPL, recorded decision + waf/CMake integration risk) vs. a hand-rolled autocorrelation-based detector (no new dependency, lower accuracy). Leaning hand-rolled; this is the one call left before the design is fully signed off.
3. Everything else above (no protocol bump, sync-button one-shot/per-deck semantics, synchronous on-load analysis, Signalsmith Stretch + Linear pinned versions, the STFT-latency pre-roll note) is a design call already made in this doc — flag here if any of those should be reopened instead of accepted as written.
