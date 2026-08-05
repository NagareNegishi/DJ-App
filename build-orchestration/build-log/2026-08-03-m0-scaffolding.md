# M0 — Project scaffolding

Milestone M0 from `docs/plan/07-milestones.md`. The repo started at its pre-M0 baseline: a
placeholder `client/src/Main.cpp`, the JUCE GUI-app CMake target, an empty `server/README.md`,
and nothing else. M0 adds the test harness, style config, CI, and the protocol version file so
later milestones have a green build to grow from.

End state: container configure + `cmake --build` + `ctest` all pass, with one smoke test green.

## What landed

- `client/CMakeLists.txt`: a Catch2 v3.8.1 `dj-app-tests` console target behind
  `option(DJAPP_BUILD_TESTS ON)`, registered with CTest via `catch_discover_tests`.
- `client/tests/SmokeTest.cpp`: one trivial Catch2 test proving the harness builds and runs.
- `.clang-format`: the exact body from `docs/plan/08-conventions.md`.
- `.gitignore`: build output, `node_modules`, secrets, and audio, plus the orchestration
  scratch dirs.
- `.github/workflows/ci.yml`: server, client, and format jobs.
- `shared/protocol/PROTOCOL-VERSION`: `1`.

## Unit cut

Four workers over disjoint file sets, so they ran in parallel with no merge contention:
`build-scaffold` (the CMake test target), `repo-config` (`.clang-format`, `.gitignore`,
`PROTOCOL-VERSION`, reformat `Main.cpp`), `ci` (the workflow), and a blackbox-tester for the
smoke test. The smoke test is a test file, so it went to the tester, never an implementer.

## Decisions and their reasoning

- **Test target is glob-guarded.** The `dj-app-tests` target is created only when
  `client/tests/*.cpp` is non-empty. An implementer builds with its test dir pruned, so without
  the guard its configure would fail on a target with no sources. The guard lets the implementer
  verify the app build in isolation; the real test-target build and link get verified at
  integration once the smoke test is in place.
- **`project()` needs C enabled.** A pristine `main` checkout failed to configure with "A C
  compiler is required to build JUCE." The fix is `LANGUAGES CXX C`. Recorded in
  `docs/plan/DEVIATIONS.md` because the plan described the target as already working.
- **The test target must opt out of curl.** `juce_core` defaults to `JUCE_USE_CURL=1` and
  references libcurl, which the test target does not link, so it failed at link with undefined
  `curl_*` symbols. Adding `JUCE_USE_CURL=0` and `JUCE_WEB_BROWSER=0` to `dj-app-tests` (the same
  opt-out the app target already has) fixed it. This was the one integration failure of the
  session and cost one fix round on `build-scaffold`.
- **CI supply-chain refs stay mutable.** Actions use `@v4` and JUCE is cloned by tag, not by
  commit SHA, and the JUCE cache key is static. Accepted for the prototype and recorded in
  `docs/decisions.md` §7: the workflow carries no secrets and uses `pull_request` (not
  `pull_request_target`), so the exposure is small. Revisit if CI gains secrets or the project
  leaves prototype status.

## How this connects forward

The Catch2 target and the `tests/` glob are the frame every later suite drops into
(serialization, ranges, StateManager, BufferPlaybackSource, repository). The client CI job runs
`ctest`; the server and format jobs are wired but dormant. The server job stays green until
`server/package.json` exists at M6. The format job is non-blocking until M5, when
`docs/plan/07-milestones.md` says to make it required.

## Accepted risk

- CI supply-chain: mutable action tags, JUCE cloned by tag, and a static cache key. Recorded in
  `docs/decisions.md` §7 with its revisit trigger.
