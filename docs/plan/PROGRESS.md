# Milestone progress

One line per completed milestone: date · milestone · result. Commit hash is the commit
that adds the line (see `git log`).

- 2026-08-04 · **M0 — Project scaffolding** · container configure + build + `ctest` green
  (1/1 smoke test); Catch2 v3.8.1 test target, `.clang-format`, `.gitignore`, CI workflow,
  and `shared/protocol/PROTOCOL-VERSION` landed. No host checklist for M0.
- 2026-08-05 · **M1 — Walking skeleton + domain model** · container + Windows host
  both green: `client/build/windows` configures/builds clean (38/38 `ctest`), JUCE
  window opens titled "DJ App" and closes cleanly. Host checklist
  (`checklists/M1-host.md`) confirmed; three deviations recorded (VS 2026 toolset,
  JUCE install path, Windows `Debug` artefact subfolder) in `DEVIATIONS.md`.
- 2026-08-05 · **M2 — AudioRepository** · container green: `client/build/linux`
  configures/builds clean, `ctest` 59/59 (repository black-box + white-box suites
  added). `LocalFileRepository` + `TrackListComponent` landed and wired into
  `MainComponent`; manifest tracks list correctly, invalid entries (bad id,
  path-escaping/symlinked file, missing file, oversized/corrupt decode) each skip
  with a distinct log line, confirmed via a headless run — app never crashes. No host
  checklist for M2. No deviations recorded.
