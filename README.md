# DJ App

A real-time, shared-state, multi-user DJ application. One user (the
**controller**) manipulates playback — play/pause, seek, gain, rate, loop
points — and every connected user hears the same thing at the same time,
each rendering audio locally from their own copy of the track files. Only
playback *state* crosses the network; audio itself never does.

- **Client**: C++20 with JUCE 8.0.12 (`client/`)
- **Sync server**: Node.js 22 + `ws` (`server/`)
- **Shared protocol fixtures**: `shared/protocol/` (wire-contract test data used by both sides)

## Status

Under active development, built milestone by milestone per
[`docs/plan/07-milestones.md`](docs/plan/07-milestones.md). Progress so far:

- **M0 — Project scaffolding**: done
- **M1 — Walking skeleton + domain model**: done (JUCE window opens on Windows and Linux)
- **M2 — AudioRepository**: done (app lists real local tracks from a manifest)
- **M3 — AudioEngine (single deck)**: done (loads a track and plays it audibly on the Windows host via temporary dev-UI controls)
- **M4 — StateManager**: done (dev-UI controls now route through `StateManager.applyDelta`; engine and sync are subscribers)

See [`docs/plan/PROGRESS.md`](docs/plan/PROGRESS.md) for the full log. Multi-user
sync (the core feature) lands at M7.

## Development environment

Development happens inside a Dev Container ([`.devcontainer/`](.devcontainer/)):
editing, compilation, and all automated tests run there. Real audio playback and
GUI interaction require a real sound device, so those are verified on a Windows
host (MSVC via VS Build Tools) using the manual checklists in
[`docs/plan/checklists/`](docs/plan/checklists/). In short: **the container
builds and tests the code; the Windows host runs the audio app.**

Windows host toolchain setup (JUCE install, VS Build Tools) is documented in
[`docs/setup.md`](docs/setup.md).

## Building and testing

Run from the repo root, inside the container.

```bash
# Client — configure, build, test
cmake -S client -B client/build/linux -G Ninja
cmake --build client/build/linux
ctest --test-dir client/build/linux --output-on-failure

# Client — run (audio/GUI meaningful on Windows host only; container run must not crash)
./client/build/linux/dj-app-client_artefacts/"DJ App"
```

The server (`npm ci && npm test`, `npm start`) comes online at M6.

On the Windows host, from the x64 Native Tools Command Prompt for VS Build Tools,
the same CMake commands work with `-B client/build/windows`.

## Documentation map

- [`docs/plan/README.md`](docs/plan/README.md) — the binding implementation spec; start here for any code change
- [`docs/decisions.md`](docs/decisions.md) — rationale behind the plan
- [`docs/plan/DEVIATIONS.md`](docs/plan/DEVIATIONS.md) — recorded departures from the plan
- [`CLAUDE.md`](CLAUDE.md) — working rules and verified commands for this repo

## License

AGPL-3.0. See [LICENSE](LICENSE).

Open to discussing dual-licensing arrangements for commercial use.
