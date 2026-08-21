# DJ App — Implementation Plan (Agent-Facing)

This folder is a complete, self-contained specification for implementing the DJ App.
A fresh agent session must be able to build the entire project from these documents alone.

## Authority and scope

- This plan **supersedes** `docs/architecture.md`, `docs/stack.md`, and `docs/setup.md` wherever they conflict. Those documents remain valuable history; do not edit them. Conflicts and their rationale are recorded in `docs/decisions.md`.
- The planning session that produced this folder was forbidden from touching code. **That restriction does not apply to you.** You are expected to create and modify code, `CLAUDE.md` (Commands section), CI config, and anything else a milestone calls for.
- If reality forces a deviation from this plan (an API doesn't exist, a version is gone, a step is impossible), do the smallest sensible alternative and record it in `docs/plan/DEVIATIONS.md` (create the file on first use: date, what the plan said, what you did, why).

## Reading order

| File | Contents |
|---|---|
| `01-architecture.md` | Layers, interfaces, threading model, target directory layout |
| `02-protocol.md` | Complete WebSocket wire protocol (single source of truth for both sides) |
| `03-server.md` | Node.js sync server specification |
| `04-client.md` | C++/JUCE client specification, layer by layer |
| `05-testing.md` | Test strategy, frameworks, CI pipeline |
| `06-security.md` | Security requirements (binding, not advisory) |
| `07-milestones.md` | Ordered milestones M0–M10 with tasks and acceptance criteria |
| `08-conventions.md` | Code style, commit style, documentation rules |

Read `01`, `07`, and `08` before writing any code. Read the others when the active milestone touches them (`07` tells you which).

## Ground rules

1. **Work milestone by milestone, in order** (`07-milestones.md`). Do not start milestone N+1 until N's acceptance criteria pass. Do not implement ahead of the current milestone "while you're in there."
2. **Every milestone ends green**: code compiles in the container, all tests pass (`ctest` + `npm test` where applicable), and the acceptance checklist is verified.
3. **Interfaces first.** Every layer boundary in `01-architecture.md` is an abstract interface. Concrete implementations live behind it. Never let JUCE, `ws`, or IXWebSocket types leak across a layer boundary except where the interface explicitly says so.
4. **The container builds and tests; the Windows host runs audio.** Anything requiring a real audio device or GUI interaction is verified via the manual checklists in `05-testing.md`, executed by the user on the host. When a milestone needs host verification, prepare everything, then ask the user to run the checklist and report results.
5. **Security rules in `06-security.md` are requirements, not suggestions.** Validation of network input is part of "done", not a hardening pass for later.
6. **Update `CLAUDE.md` → Commands** whenever a build/test/run command is added or changed (M0 fills it in first).
7. **Commit per coherent unit of work** with messages per `08-conventions.md`. Never commit audio files, build output, or secrets.

## Current repository state (as of plan authoring, 2026-07-06)

- `client/CMakeLists.txt` — working JUCE 8 GUI-app target (`dj-app-client`), links `juce_gui_basics` only; `client/src/Main.cpp` is a console placeholder (`int main` printing a line). M1 replaces it.
- `client/tests/` — empty directory, no test scaffold yet.
- `server/` — empty except README placeholder.
- Dev container has JUCE 8.0.12 installed to `/usr/local`, CMake 3.28, Ninja, clang/gcc. Build commands (verify, then move into `CLAUDE.md`):
  ```
  cmake -S client -B client/build/linux -G Ninja
  cmake --build client/build/linux
  ```
- No CI, no `.clang-format`, no shared protocol fixtures yet.

## Fixed versions and constants

Use these everywhere; do not re-decide them silently:

| Item | Value |
|---|---|
| JUCE | 8.0.12 (installed in container; same tag on Windows host) |
| C++ standard | C++20 |
| CMake minimum | 3.28 |
| Node.js | 22 LTS |
| `ws` | latest 8.x, pinned via `package-lock.json` |
| Catch2 | v3.8.1 via CMake FetchContent (pin the tag; if unavailable, nearest v3.x and record in DEVIATIONS) |
| IXWebSocket | latest v11.x tag via FetchContent, pinned (record exact tag in DEVIATIONS if plan-time guess is stale) |
| Protocol version | 2 (bumped M9 for `repeat`) |
| Server default bind | `127.0.0.1:8765` |
