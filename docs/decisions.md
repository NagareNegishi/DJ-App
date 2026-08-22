# Planning Decisions — DJ App

*2026-07-06. Human-facing companion to `docs/plan/`. That folder tells an agent what to build; this document explains what was decided while writing it, where it departs from the original `docs/architecture.md` / `docs/stack.md` / `docs/setup.md`, and why. The original documents are untouched and remain useful history; where they conflict with `docs/plan/`, the plan wins.*

---

## 1. Corrections to the original docs

### 1.1 The prototype audio engine is JUCE, not Web Audio

`docs/architecture.md` says the prototype AudioEngine implementation is "Web Audio API (browser)". That line predates the decision (visible in `docs/stack.md`, `CLAUDE.md`, and the actual repo) to build the client in C++/JUCE from the start. There is no browser client on the roadmap. The plan resolves this by specifying a JUCE-native engine, split into a pure DSP core (`BufferPlaybackSource`, unit-testable in the container with no audio device) and thin device wiring. Everything else in the architecture doc — the five layers, the contracts, optimistic updates — carries over unchanged.

### 1.2 JUCE does not have the WebSocket client the stack doc assumed

`docs/stack.md` open decision 1 was "JUCE built-in WebSocket vs external library — try JUCE first." On verification, JUCE 8 offers raw TCP (`StreamingSocket`) and HTTP (`juce::URL`) but no WebSocket *client* API, so "try JUCE first" would mean hand-rolling the WebSocket protocol — the worst of both options. The plan selects **IXWebSocket** (BSD-3, actively maintained, small, CMake-friendly, TLS-capable later). Alternatives considered: `websocketpp` (effectively unmaintained, wants Asio), `Boost.Beast` (excellent but drags in Boost for one feature). The choice is low-risk regardless: it sits entirely behind the `SyncTransport` interface, so swapping costs one file.

## 2. Decisions the original docs deferred, now resolved

### 2.1 Conflict resolution → a single enforced controller

The architecture doc deferred "conflict resolution when two users change the same parameter simultaneously." The product statement itself ("one user controls, all users observe") already implies the cleanest answer: at most **one controller at a time, enforced by the server**. Deltas from non-controllers are rejected and answered with an authoritative snapshot that repairs the sender's optimistic state. This turns a hard distributed-systems problem into a role check, at the cost of not supporting simultaneous four-hands mixing — acceptable, since that was never the stated product.

### 2.2 Position is not streamed

A naive sync design streams the playhead position continuously; that floods the network and still stutters. Instead, deltas carry position only at meaningful moments (play, seek), every client advances the playhead locally from `position + elapsed × rate`, and the controller sends a small correction every 5 seconds. Drift between corrections is accepted for the prototype. This mirrors how the app treats audio generally: state syncs, computation is local.

### 2.3 Whole-track in-memory buffers

Tracks are fully decoded into memory on load rather than streamed from disk. That's the standard DJ-software approach (instant seek, loop, and scratch-style access) and it makes the DSP core trivially testable. The cost — a few hundred MB for long tracks — is fine on desktop.

## 3. What the plan adds that no original doc covered

| Addition | Why it was the gap that mattered |
|---|---|
| **Complete wire protocol** (`plan/02-protocol.md`) | Two codebases in two languages need one written contract: message shapes, a canonical field/range table, close codes, rate limits, handshake. Without it, the client and server would each invent half a protocol. It's the single source of truth; shared JSON fixtures test both sides against it. |
| **Threading model** (`plan/01`) | Real-time audio's classic failure mode is a lock or allocation on the audio callback. The plan makes the three thread domains (message/audio/network) and their handoff rules (atomics, marshal-to-message-thread) explicit and binding, instead of leaving them to be discovered via glitches. |
| **Security baseline** (`plan/06`) | Networked app, public repo. Scope is deliberately modest — validate both directions, never turn network data into file paths, bind localhost by default, room code, size/rate/connection limits, pinned dependencies — with an explicit "out of scope" list so nobody gold-plates. Notably the *client* validates too: the server is not trusted either. |
| **Testing strategy + CI** (`plan/05`) | Original docs had a `tests/` directory and no plan. Now: Catch2 for C++ (offline-rendered DSP tests included), `node:test` for the server (zero extra dependencies), shared contract fixtures, GitHub Actions, and manual host checklists for what a container genuinely cannot verify (real audio, GUI). |
| **Milestones with acceptance criteria** (`plan/07`) | "Build order 1–9" became M0–M10 with per-milestone tasks, tests, and verifiable done-conditions — what an implementing agent needs to know when to stop. |
| **Conventions** (`plan/08`) | clang-format file, naming, error-handling policy, commit style, and living-docs rules (DEVIATIONS.md, PROGRESS.md, CLAUDE.md commands kept current). |

## 4. Notable reordering

**The data model moves ahead of repository and engine** (M1, vs. steps 1–3 in the original build order). Every layer consumes `PlaybackState`/`StateDelta`, the model is pure and fully unit-testable on day one, and the wire protocol pins its field names — so building it first means repository, engine, and server all code against final types. Also, **testing/CI scaffolding (M0) precedes everything**, and **the server (M6) is built and fully tested before the client grows a network stack (M7)** so each side of the protocol meets a known-good counterpart.

## 5. Smaller technology choices (with alternatives)

| Choice | Picked | Over | Because |
|---|---|---|---|
| Client JSON | `juce::JSON`/`juce::var` | nlohmann/json | Already shipped with JUCE; protocol is small; one less dependency to pin and license-audit |
| C++ test framework | Catch2 v3 (BSL-1.0) | GoogleTest, JUCE UnitTest | Modern, header-friendly, first-class CTest integration; JUCE's own runner is too bare (no CLI filtering/reporting) |
| Server validation | hand-rolled, table-driven | ajv/zod | ~7 fields with simple ranges; a schema library is more surface than the problem |
| Server test framework | built-in `node:test` | vitest/jest | Keeps the server at exactly one runtime dependency (`ws`) |
| Wire format | JSON text frames | binary/CBOR | Human-debuggable (`wscat`), trivially fixture-testable; bandwidth is negligible at ≤ 8 clients × ≤ 60 msg/s |
| Delta broadcast | to everyone *except* sender | echo-to-all | Matches the architecture doc and the optimistic-update principle; halves controller traffic. Sequence-gap detection + snapshot request covers the (unlikely, TCP) resync case |
| Room security | single room, startup-generated join code | open server / full auth | One shared secret is the right amount of security for a LAN prototype and costs ~10 lines |
| Crossfader sync (M8) | local-only, not synced | protocol v2 field | Syncing it properly needs a protocol v2 bump touching fixtures and both sides' wire handling for a mixer-level concept with no home in per-deck state — not the trivial case that would have justified doing it now. See `DEVIATIONS.md` 2026-08-21 |

## 6. Explicitly *not* decided (unchanged from `docs/stack.md`)

The stack doc's deferrals stand, with their original triggers: ASIO, extra audio formats, production server language. Time-stretch and beat-detection libraries were resolved at the M10 design gate — see §6.1 below. Beat alignment (M10) additionally gets a mandatory design-review gate before any code, because it is the one milestone with real algorithmic risk and a license landmine.

### 6.1 M10 design gate: time-stretch and beat-detection libraries resolved

Full design in `docs/plan/10-beatsync-design.md`; summary here per `CLAUDE.md`'s "GPL beyond JUCE needs a recorded decision first" rule and `07-milestones.md`'s M10 text requiring the beat-detection license call be recorded here before integrating.

- **Time-stretch: Signalsmith Stretch (MIT), not SoundTouch (LGPL).** `docs/stack.md`'s original default was SoundTouch; the design gate is exactly where that got revisited, and it didn't hold up. Signalsmith Stretch and its one dependency, Signalsmith Linear, are both MIT — no LGPL dynamic-link obligation to manage — and header-only, so integration is plain `FetchContent` with no build-system risk. It's also the better dependency on reliability grounds: OpenMPT (a long-running, technically rigorous open-source tracker) replaced SoundTouch with it in production specifically because SoundTouch's time-domain approach produced audible artifacts, and independent developer feedback (KVR Audio) rates its quality at or above Rubber Band (the higher-quality GPL alternative) at a fraction of the CPU cost. Pin: `signalsmith-stretch` tag `1.1.0`, `signalsmith-linear` tag `0.6.0` (pinned explicitly and separately — `signalsmith-stretch`'s own build transitively fetches an older `0.3.1`).

- **Beat detection: qm-dsp (GPL-2.0-or-later) — the required recorded decision.** `docs/stack.md` named aubio as the default beat-detection library, citing it as "used by Mixxx." That's stale: verified against Mixxx's actual analyzer source, Mixxx does not use aubio — it uses Queen Mary's `qm-dsp` (primary) and SoundTouch's `BPMDetect` (legacy fallback, BPM-only). aubio itself has had no tagged release since 2019 and is waf-built (real CMake integration friction); it was dropped from consideration on that basis, independent of the license question. qm-dsp is GPL-2.0-or-later (the "-or-later" gives the same GPLv3-combination compatibility path with this project's AGPL-3.0 that GPLv3-licensed code would have). Decision: **proceed with qm-dsp**, on the strength of the direct Mixxx production precedent (same two classes, `DetectionFunction` + `TempoTrackV2`) and a verified minimal, self-contained dependency footprint for that specific path (bundled `kissfft` only — no FFTW, Vamp SDK, or Boost required to build it). No tagged release exists for qm-dsp's DSP core, so integration pins an exact commit SHA on `c4dm/qm-dsp` `master` rather than a tag — a deviation from the project's tag-pinning convention, accepted here because qm-dsp is small, academically mature, and slow-moving, unlike e.g. Essentia (AGPL-3.0, also evaluated and rejected: no official CMake, no release since 2019 either, and its only CMake path is an unmerged third-party fork — materially higher integration risk than qm-dsp for the same untagged-pin problem).
- `BeatDetector` is designed as a swappable interface specifically so this isn't permanent: a hand-rolled autocorrelation-based detector (no dependency at all, same algorithm family qm-dsp and SoundTouch's `BPMDetect` both use) is the documented future replacement if the GPL dependency or the untagged pin ever becomes a real maintenance cost.
- **RT-safety verification, required by the design gate's own text: PASS, no fallback needed.** `10-beatsync-design.md`'s RT-safety section named a non-optional implementation-time task - confirm `SignalsmithTimeStretcher::process()` allocates nothing once `prepare()` has run, at the app's real block sizes, and fall back to a worker-thread + lock-free ring buffer (its "Option B") if it doesn't. `client/tests/engine/SignalsmithTimeStretcherAllocationTest.cpp` (an allocation-counting override scoped tightly around `process()` alone) confirms zero heap allocations across a spread of block sizes (64-2048 frames) and tempo ratios (`playbackRateMin`/`playbackRateMax`/unity), both single-call and across 200 consecutive steady-state calls. This holds via `SignalsmithStretch::configure()`'s own internal pre-sizing (invoked from `prepare()`), not a `Linear::reserve()` call - see the next point. Option B was not needed.
- **Signalsmith Stretch v1.1.0 (the pinned tag) does not depend on Signalsmith Linear at all**, contrary to `10-beatsync-design.md`'s `Linear::reserve()`-based RT-safety reasoning: at this pinned commit, `signalsmith-stretch.h` pulls in only its own bundled, older "signalsmith-dsp" headers, with no Signalsmith Linear include anywhere on that path. `SignalsmithTimeStretcher.cpp`'s own header comment records this; the RT-safety claim above still holds, just via a different pre-sizing mechanism than the design doc assumed.

## 7. Risks accepted knowingly

- **`ws://` unencrypted for the prototype**, mitigated by localhost-default binding and the room code; the TLS path (reverse proxy) is documented but unbuilt.
- **Playback-rate changes shift pitch** until M10 — correct behavior for a prototype without a time-stretcher; `pitchOffsetSemitones` is carried in the state/protocol from day one so adding rendering later is not a breaking change.
- **Observer playhead drift** up to the 5-second correction interval.
- **Self-stop correction is role-blind** (M5 bugfix, `EngineAdapter::checkForSelfStop`): every client, not just the controller, locally corrects its own `playing` flag when its engine stops itself at end-of-track. Until M6/M7 add real rooms and a `playing`-bearing resync, an observer whose local copy of a track ends slightly out of step with the controller's has no automatic way to reconcile that local correction against the room. Accepted for the prototype, same category as playhead drift above; revisit once role-awareness is actually exercised by a live multi-client session.
- **`state.positionSeconds` stays stale after a self-stop correction**: the same fix corrects `playing` only, not position — `MainComponent::computeResumePositionSeconds()` still reads the engine directly for resume decisions regardless, so nothing currently consumes the stale value, but a future direct reader of `StateManager`'s position (e.g. a multi-user broadcast) would see it. Deferred to M7's `PositionClock`, which resyncs position properly.
- **`StateDelta::loop` cannot express "armed but not confining"** (M5 bugfix, `DeckComponent`'s non-destructive loop re-arm): the protocol only has absent/explicit-clear/complete-pair for a loop, with no slot for "these points exist but aren't enforced right now." That gap is why re-arming a loop has to stash the previous points as private `DeckComponent` state, invisible to other clients, rather than something the shared model could represent. A protocol change would need its own version bump per `docs/plan/02-protocol.md` and is disproportionate to a UI bug fix; revisit if a future milestone needs peers to see an in-progress loop edit.
- **IXWebSocket pin and Catch2 pin** were chosen from planning-time knowledge; if a tag has moved on, the implementing agent records the substitution in `docs/plan/DEVIATIONS.md` rather than stalling.
- **CI uses mutable upstream refs** (M0): third-party actions pinned to major tags (`actions/checkout@v4`, `setup-node@v4`, `cache@v4`) and JUCE cloned by tag (`--branch 8.0.12`), rather than to immutable commit SHAs; the JUCE build cache key (`juce-8.0.12-ubuntu24`) is likewise static with no content hash. A moved tag or poisoned cache entry could therefore enter a build undetected. Accepted for the prototype: the workflow carries no secrets and uses `pull_request` (not `pull_request_target`), so fork PRs run with a read-only token; the actions are official. Revisit (SHA-pin actions + JUCE, key the cache to the JUCE SHA) if secrets are ever added to CI or the project leaves prototype status.
