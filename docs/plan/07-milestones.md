# 07 — Milestones

Execute strictly in order. Each milestone lists Goal / Tasks / Acceptance. "Green" always means: client configures + builds in the container, `ctest` passes, `npm test` passes (once the server exists), CI passes. Where a **Host checklist** is listed, prepare `docs/plan/checklists/M<k>-host.md` per `05-testing.md` and get the user's pass confirmation before closing the milestone.

Note on ordering vs `docs/architecture.md`: the data model is built **before** repository and engine (it's pure, testable, and everything depends on the types). Rationale in `docs/decisions.md`.

---

## M0 — Project scaffolding

**Goal:** testing, CI, and conventions exist before any feature code.

**Tasks**
1. Add `dj-app-tests` Catch2 target + one trivial passing test (`tests/SmokeTest.cpp`) wired into CTest (`04-client.md` CMake notes).
2. Create `.clang-format` at repo root (`08-conventions.md` contains the file body) and format the existing `client/src`.
3. Verify/extend `.gitignore`: `client/build/`, `.vs/`, `client/assets/tracks/*` with `!manifest.example.json`, `node_modules/`.
4. Create `.github/workflows/ci.yml` per `05-testing.md` (server job guarded until server exists).
5. Adopt the plan's `CLAUDE.md` (done — it now lives at the root `CLAUDE.md`; the pre-plan version is archived at `docs/CLAUDE-original.md`), then verify every command in its Commands section by running it (fix the doc where reality differs, e.g. the artefact path).
6. Create `shared/protocol/PROTOCOL-VERSION` containing `1`.

**Acceptance:** container build green; `ctest` runs 1 test green; CI green on push; `CLAUDE.md` commands verified by running each one.

---

## M1 — Walking skeleton + domain model

**Goal:** a real JUCE window on both platforms; the complete data model with serialization.

**Tasks**
1. Replace placeholder `Main.cpp` with `JUCEApplication` + `DocumentWindow` + empty `MainComponent` (title "DJ App"). Keep it building on Linux.
2. Implement `model/` completely: types, `Serialization`, `Ranges` (`04-client.md`).
3. Unit tests: serialization + ranges suites (`05-testing.md`).
4. Write `docs/plan/checklists/M1-host.md`: Windows configure/build (Ninja + MSVC per `docs/setup.md`), window opens, closes cleanly.

**Acceptance:** ctest green (model suites); Linux build green; **Host checklist** M1 confirmed (window runs on Windows).

---

## M2 — AudioRepository

**Goal:** app lists real local tracks.

**Tasks**
1. Implement `repository/` per `04-client.md` incl. all path-safety rules; commit `manifest.example.json` + assets gitignore entries.
2. `TrackListComponent` in the window showing repository contents (no loading into audio yet — selection just logs).
3. Unit tests: repository suite (manifest, traversal rejection, caching) using a temp directory fixture with tiny generated WAVs (write them from the test via `juce::AudioFormatWriter`).

**Acceptance:** ctest green; user (or you, via a headless list dump) sees manifest tracks listed; invalid manifest entries skipped with log, app doesn't crash.

---

## M3 — AudioEngine (single deck, real sound)

**Goal:** load a track and hear it on the Windows host; DSP core fully unit-tested in the container.

**Tasks**
1. Implement `BufferPlaybackSource`, `JuceAudioEngine`, `AudioDeviceHub` per `04-client.md`, respecting every audio-thread rule in `01-architecture.md`.
2. Temporary dev UI on `MainComponent`: load-selected, play/pause, seek, gain, rate controls calling the engine **directly** (StateManager arrives in M4; mark this wiring `// M4 replaces`).
3. Unit tests: full BufferPlaybackSource offline-render suite (`05-testing.md`).
4. Checklist `M3-host.md`: audio plays, pause/resume, seek audibly jumps, gain slider works, rate 0.5/2.0 audibly slower/faster (pitch shifts — expected), end-of-track stops, no crash on device absence (test in container run too).

**Acceptance:** ctest green; **Host checklist** M3 confirmed.

---

## M4 — StateManager

**Goal:** all state flows through one place; engine and UI become subscribers.

**Tasks**
1. Implement `state/StateManager` + `EngineAdapter`; rewire M3's dev UI to emit `StateDelta`s instead of calling the engine (delete the `// M4 replaces` wiring).
2. Wire `NullTransport` + `SyncPublisher` into the composition root (publisher is a no-op path until M7, but the subscription topology is final).
3. Unit tests: StateManager suite (`05-testing.md`).

**Acceptance:** ctest green; behavior on host identical to M3 (quick re-run of M3 checklist by user, or container-side verification that all UI paths route through `applyDelta` — grep: no `ui/` file may include `engine/`).

---

## M5 — Single-deck UI (real)

**Goal:** the actual `DeckComponent` + `TrackListComponent` per `04-client.md`, replacing dev UI.

**Tasks**
1. Build `ui/DeckComponent` (playhead timer, sliders, loop buttons) and integrate with StateManager; delete dev UI.
2. Loop feature end-to-end (state → engine loop wrap) — first time loop is exercised audibly.
3. Flip CI `format` job to required. Add regression tests for any bugs found.
4. Checklist `M5-host.md`: full manual pass of every control incl. loop capture/clear, position slider seek, disabled-state rendering.

**Acceptance:** ctest + CI green; **Host checklist** M5 confirmed. This closes "single-track prototype".

---

## M6 — Server + protocol

**Goal:** complete, tested sync server; no client changes yet.

**Tasks**
1. Implement `server/` exactly per `03-server.md`.
2. Populate `shared/protocol/fixtures/` per `05-testing.md`; wire fixture runs into server tests; add the client-side fixture test (parsing server messages) to ctest.
3. Enable the CI server job. Update `CLAUDE.md` Commands (server start/test).
4. Write `server/README.md` content: run instructions, env vars, production TLS posture note (replaces placeholder — the old file is empty; creating real content here is expected, not a violation of "don't touch existing docs", which bound only the planning session).

**Acceptance:** `npm test` green incl. all fixtures; ctest green incl. client fixture suite; manual smoke: start server, connect twice with `wscat` or a 10-line node script, watch delta broadcast and role enforcement.

---

## M7 — Multi-user sync on one deck

**Goal:** two app instances, one controller, shared state, audible on both.

**Tasks**
1. Implement `WebSocketTransport` (IXWebSocket via FetchContent, pinned) + `ConnectPanel` + role-based UI disable + snapshot application + `PositionClock` resync per `04-client.md`.
2. Reconnect handling: connection drop surfaces in UI with a reconnect button (no auto-reconnect loop needed for prototype).
3. Tests: transport message building/parsing unit tests (against fixtures); integration is manual.
4. Checklist `M7-host.md`: server in container; two client instances on Windows host (same machine acceptable), both with same local audio files; claim control on one; play/pause/seek/gain/rate/loop mirror on the other within ~100 ms; observer controls disabled; controller disconnect frees control; observer with missing track shows "missing track" and stays silent.

**Acceptance:** all suites green; **Host checklist** M7 confirmed. This is the product's core promise working.

---

## M8 — Second deck + mixer

**Goal:** two decks, crossfader, synced.

**Tasks**
1. Instantiate deck B end-to-end (engine, state, UI) — should be wiring, not refactoring; if it forces refactors, the deck-keying rule from `01-architecture.md` was violated somewhere: fix that properly.
2. `MixerComponent` with equal-power crossfader; crossfader position is **local-only state for now** (not in protocol v1) — display-only mismatch across users is accepted and documented in DEVIATIONS… **unless** trivial: adding it properly requires a protocol field, version bump to 2, fixture updates. Decide by effort; record either way.
3. Server already supports deck B (lazy creation) — add integration test exercising it.
4. Checklist `M8-host.md`: two tracks simultaneously, crossfade, both decks mirrored on observer.

**Acceptance:** all suites green; **Host checklist** M8 confirmed.

---

## M9 — Beat alignment (design-gate, then build)

**Goal:** tempo-match + phase-align deck B to deck A. **Hard; deliberately last; do not start early.**

This milestone begins with a **design review, not code**: write `docs/plan/09-beatsync-design.md` covering: `TimeStretcher` interface (rate change without pitch change) + `BeatDetector` interface; library choice per `docs/stack.md` triggers (default: SoundTouch for stretch — LGPL, dynamic-link it; aubio for beats — GPL, license decision required and must be recorded in `docs/decisions.md` follow-ups before integrating); where stretching sits in `BufferPlaybackSource`; sync-button semantics (match BPM, then nudge phase to nearest beat); what `pitchOffsetSemitones` finally does. Get user sign-off on that design, then implement behind the existing interfaces with the same test discipline.

**Acceptance:** design doc approved; implementation green + host checklist with two beat-matched tracks.
