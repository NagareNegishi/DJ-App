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
