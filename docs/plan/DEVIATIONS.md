# Deviations from the plan

Each entry: date, what the plan said, what was done instead, why.

## 2026-08-03 — `client/CMakeLists.txt` needs `LANGUAGES ... C`

- **Plan said**: `docs/plan/README.md` describes the existing `client/CMakeLists.txt` as a
  "working JUCE 8 GUI-app target"; M0 treats CMake changes as additive.
- **Actual**: A pristine `main` checkout fails to *configure*:
  `CMake Error: A C compiler is required to build JUCE. Add 'C' to your project's
  LANGUAGES.` The `project()` call declared only `LANGUAGES CXX`.
- **Change**: `project(DjAppClient VERSION 0.1.0 LANGUAGES CXX)` →
  `LANGUAGES CXX C`. JUCE's Linux modules require a C compiler to be enabled.
- **Why**: Without it neither the app nor the test target can configure, so it is a
  prerequisite for every M0 acceptance command. Made as part of the M0 `build-scaffold`
  unit (the file was already in that unit's scope).

## 2026-08-04 — `model/Serialization::fromVar` clamps out-of-range numbers instead of rejecting them

- **Plan said**: `docs/plan/04-client.md`'s `model/` section says parsing is strict and
  should "reject unknown fields, wrong types, non-finite numbers, out-of-range values
  (return failure, never partially fill)" — out-of-range values listed alongside the
  other reject cases.
- **Plan also said**: `docs/plan/02-protocol.md`'s Validation policy says the opposite for
  the client side: "client clamps incoming values into range as defense-in-depth", with
  hard rejection reserved for the server. These two statements contradict each other for
  the client.
- **Actual**: `fromVar<T>` only rejects unknown fields, wrong JSON types, and non-finite
  numbers. A well-typed but out-of-range value (e.g. `gain: 5.0`) parses successfully;
  `Ranges::clamp()` is the range-enforcement step, applied by the caller after a
  successful parse. The one exception is `loop.inSeconds < outSeconds`, which can't be
  fixed by clamping two independent endpoints and so is rejected in `fromVar` itself.
- **Why**: Followed `02-protocol.md`, since its Validation policy section states the
  client/server asymmetry explicitly and by name, while `04-client.md`'s line reads more
  like shorthand for "parsing is strict" than a deliberate restatement of that policy.
  This is also the only reading under which `Ranges::clamp()` has a real job on incoming
  network data: if `fromVar` already rejected out-of-range values, `clamp()` would only
  ever run on locally-constructed state. Decided with the user; see the M1 build log
  (`build-orchestration/build-log/2026-08-04-m1-model-appshell.md`) for the full
  discussion.

## 2026-08-05 — Windows host has VS Build Tools 2026 (v18), not VS 2022

- **Plan said**: `docs/setup.md` and `docs/plan/checklists/M1-host.md` pin
  Visual Studio Build Tools 2022.
- **Actual**: Host has VS 2026 Build Tools (v18.6.1), MSVC `19.51.36244`; VS
  2022 was never installed.
- **Why**: The pin wasn't feature-specific — it just needed `cl.exe`,
  Windows SDK, MSBuild, and Ninja with C++20 support, which VS 2026's MSVC
  toolset provides as a backward-compatible superset. Proceeded as-is.

## 2026-08-05 — JUCE installs to `Program Files (x86)`, not `Program Files`

- **Plan said**: `docs/setup.md` and `docs/plan/checklists/M1-host.md` state
  an unset `CMAKE_INSTALL_PREFIX` defaults to `C:\Program Files\JUCE`.
- **Actual**: `cmake --install C:\JUCE\build` (Ninja generator) installed to
  `C:\Program Files (x86)\JUCE` instead.
- **Why**: With the Ninja generator, CMake picks the default install prefix
  before the compiler-detection step confirms 64-bit, so it falls back to
  the 32-bit path. Harmless: CMake's `WindowsPaths.cmake` adds both
  `Program Files` and `Program Files (x86)` to the default `find_package`
  search path, so `find_package(JUCE CONFIG REQUIRED)` still finds it. No
  action taken.

## 2026-08-05 — Windows artefact path nests a `Debug` subfolder

- **Plan said**: `docs/plan/checklists/M1-host.md` (step 4) states the
  artefact path has no `Debug`/`Release` subfolder on Windows, reasoning
  that Ninja is single-config so JUCE won't nest a build-type folder —
  matching the container's actual path
  (`client/build/linux/dj-app-client_artefacts/DJ App`).
- **Actual**: On Windows the built exe lands at
  `client/build/windows/dj-app-client_artefacts/Debug/DJ App.exe` — JUCE
  nests a `Debug` folder here despite Ninja being single-config.
- **Why**: Not yet root-caused; `CMAKE_BUILD_TYPE` was left unset for this
  configure, and the JUCE install itself also logged `Install
  configuration: "Debug"` under the same conditions, suggesting JUCE's
  CMake helpers append `$<CONFIG>` to the output directory regardless of
  generator. Flagging as a deviation per the checklist's own instruction
  rather than silently substituting the path.
