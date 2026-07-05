# CLAUDE.md (proposed)

> Drop-in replacement for the root `CLAUDE.md`, written for the implementation phase driven by `docs/plan/`. Adopt at M0 by copying this file's content over the root `CLAUDE.md` (that adoption is an M0 task for the implementing session; the planning session that authored it was not allowed to modify the original).

## Project Overview

A real-time, shared-state, multi-user DJ application. One user (the **controller**) manipulates playback; all connected users see and hear the same state changes instantly, each rendering audio locally from their own copy of the files. Only state crosses the network — never audio.

- **Client**: C++20 with JUCE 8.0.12 (`client/`)
- **Sync server**: Node.js 22 + `ws` (`server/`)
- **Shared protocol fixtures**: `shared/protocol/` (wire-contract test data for both sides)
- **Build system**: CMake ≥ 3.28 + Ninja
- **License**: AGPL-3.0 — new dependencies must be permissive/LGPL; anything GPL beyond JUCE needs a recorded decision first

Dev environment is a Dev Container (`.devcontainer/`): editing, compilation, all automated tests, and the sync server run here. Real audio playback and GUI testing happen on the Windows host (MSVC via VS Build Tools 2022) via the manual checklists in `docs/plan/checklists/`. Container builds and tests the code; host runs the audio app.

## Source of truth

1. `docs/plan/` — **the binding spec.** Read `docs/plan/README.md` first; it gives reading order and ground rules. Work strictly milestone-by-milestone per `docs/plan/07-milestones.md`.
2. `docs/decisions.md` — why the plan is the way it is (human-facing rationale).
3. `docs/architecture.md`, `docs/stack.md`, `docs/setup.md` — original planning history. Do not edit; where they conflict with `docs/plan/`, the plan wins (known conflicts are listed in `docs/decisions.md`).

Deviations from the plan go in `docs/plan/DEVIATIONS.md`; milestone completions in `docs/plan/PROGRESS.md`.

## Commands

Run from the repo root inside the container. Entries marked with a milestone appear once that milestone lands — verify and update this section as part of each milestone.

```bash
# Client — configure, build, test
cmake -S client -B client/build/linux -G Ninja
cmake --build client/build/linux
ctest --test-dir client/build/linux --output-on-failure     # from M0

# Client — run (GUI/audio meaningful on Windows host only; container run must not crash)
./client/build/linux/DjAppClient_artefacts/dj-app-client    # verify artefact path on first build

# Server (from M6)
cd server && npm ci && npm test
npm start          # binds 127.0.0.1:8765; prints room code
```

Windows host build (user-driven, from a VS Build Tools Developer Command Prompt): same CMake commands with `-B client/build/windows`; JUCE 8.0.12 must be installed per `docs/setup.md`.

## Non-negotiable rules

- **Milestone discipline**: don't start milestone N+1 until N's acceptance criteria pass, including any Windows-host checklist confirmed by the user. Every milestone ends with build + `ctest` (+ `npm test`) + CI green.
- **Layer boundaries**: repository, engine, and transport stay behind their interfaces (`docs/plan/01-architecture.md`). UI code never includes `engine/`; all state changes flow through `StateManager.applyDelta`. JUCE/`ws`/IXWebSocket types don't leak across boundaries.
- **Threading**: StateManager is message-thread-only; the audio callback (`BufferPlaybackSource::getNextAudioBlock`) takes no locks, allocates nothing, logs nothing; network callbacks only parse-and-marshal via `MessageManager::callAsync`.
- **Protocol changes**: `docs/plan/02-protocol.md` is the single source of truth. Change it first, then server + client + `shared/protocol/fixtures/` in the same milestone; bump the version.
- **Security is part of done** (`docs/plan/06-security.md`): validate and clamp all network input on both sides; network data never becomes a file path; server binds localhost by default; keep dependencies pinned.
- **Testing**: every bug fix ships with a regression test in the same commit; protocol handlers and public model/state functions are covered (`docs/plan/05-testing.md`).
- **Never commit**: `build/`, `node_modules/`, audio files, secrets. Style per `docs/plan/08-conventions.md` (`.clang-format` at repo root; commit subjects prefixed with the milestone, e.g. `M3:`).
