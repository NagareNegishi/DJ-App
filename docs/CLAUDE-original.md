# CLAUDE-original.md

> Archived copy of the original root `CLAUDE.md` from the planning phase. No longer read by Claude Code — kept for history. The active instructions live in the root `CLAUDE.md`, adopted from `docs/plan/` at M0.

## Project Overview

A real-time, shared-state, multi-user DJ application. One user controls audio playback, all connected users see and hear state changes instantly.

- **Client**: C++ with JUCE 8 (`client/`)
- **Sync server**: Node.js with `ws` (`server/`)
- **Build system**: CMake + Ninja
- **License**: AGPL-3.0

Dev environment runs in a Dev Container (`.devcontainer/`) for code editing, compilation, tests, and the sync server. Audio playback and GUI testing happen on the Windows host (MSVC via Visual Studio Build Tools 2022). Container builds the code, host runs the audio app.

## Commands

To be filled in once `client/CMakeLists.txt` and `server/` exist.

## Architecture, Stack, Setup

All design decisions and rationale live in `docs/`:

- `docs/architecture.md` — layers, contracts, data flow, build order
- `docs/stack.md` — tooling choices and deferred libraries
- `docs/setup.md` — repo, devcontainer, and toolchain decisions

Read the relevant doc before making changes in an area it covers.