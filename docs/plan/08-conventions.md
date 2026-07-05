# 08 — Conventions

## C++ style

- C++20, no compiler extensions (already enforced in CMake). Warnings come from `juce::juce_recommended_warning_flags`; fix warnings, don't suppress, except with a commented rationale.
- RAII everywhere: no raw `new`/`delete`; `std::unique_ptr`/`std::shared_ptr`; `juce::Component` children as direct members where lifetime allows.
- Naming: `PascalCase` types/files, `camelCase` functions/variables, `kPascalCase` for constants is **not** used — use `constexpr` in `namespace djapp::ranges` etc. with plain camelCase. One class per header where reasonable; header `#pragma once`.
- No exceptions across layer boundaries for expected failures — return `std::optional`/`juce::Result`/null `shared_ptr` and log. Exceptions may exist inside a layer for programmer error.
- Audio-thread code (only `BufferPlaybackSource::getNextAudioBlock` and helpers it calls): no locks, no allocation, no `juce::String`, no logging, no virtual dispatch you can avoid. Comment the top of the method with `// audio thread — see docs/plan/01-architecture.md`.
- Comments state constraints and non-obvious rationale only; match existing CMakeLists comment tone. No change-log comments, no restating the next line.

## `.clang-format` (create at M0, repo root, exactly this)

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 120
BreakBeforeBraces: Allman
PointerAlignment: Left
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: false
NamespaceIndentation: None
SortIncludes: CaseSensitive
```

## JavaScript style (server)

- ES modules, `const` by default, no classes where a closure/map does, small pure functions in `validate.js`/`protocol.js`.
- No linter dependency for now; match the style of the first files you write and keep it consistent. If drift becomes a problem, adding eslint is a DEVIATIONS-recorded change.

## Git

- Branch: work directly on a feature branch per milestone (`m3-audio-engine`), PR to `main` when the milestone's acceptance passes; or commit sequentially to the current working branch if the user prefers — ask once at M0, then stick with it.
- Messages: imperative, ≤ 72-char subject, body explains *why* when non-obvious. Reference the milestone (`M3:`) as prefix. End with the Claude Code co-author trailer per harness rules.
- Never commit: `build/`, `node_modules/`, audio files, anything in `.gitignore`, generated JUCE artefacts.

## Documentation upkeep (part of every milestone)

- `CLAUDE.md` → Commands: always current; verified by execution, not memory.
- `docs/plan/DEVIATIONS.md`: every divergence from this plan (date, planned, actual, why).
- Layer README stubs are not required; instead each `src/<layer>/` interface header carries a short top comment stating the layer's single responsibility and its threading rules.
- When a milestone completes, append one line to `docs/plan/PROGRESS.md` (create at M0): date, milestone, commit hash, checklist result.
