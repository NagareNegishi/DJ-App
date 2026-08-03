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
