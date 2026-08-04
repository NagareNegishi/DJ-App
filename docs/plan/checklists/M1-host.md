# M1 host checklist — walking skeleton window

Run on the Windows host, from a Developer Command Prompt (Visual Studio Build
Tools 2022). Confirms the JUCE window builds and runs on the platform that
actually plays audio — the container only proves it compiles.

Prerequisites (one-time, skip if already done): JUCE 8.0.12 installed per
`docs/setup.md` "Windows Host Toolchain" / "Windows developer setup".

1. Pull the latest `claude` branch and open a Developer Command Prompt at the
   repo root.
   - Expected: `client/src/Main.cpp`, `client/src/app/MainWindow.*`,
     `client/src/app/MainComponent.*` are present (not the old placeholder
     console app).
2. Configure: `cmake -S client -B client/build/windows -G Ninja`
   - Expected: configures without error; reports JUCE found via
     `find_package`.
3. Build: `cmake --build client/build/windows`
   - Expected: `dj-app-client` and `dj-app-tests` both build clean, no
     errors. Warnings are worth flagging back but not blocking.
4. Run the client artefact (path per `CLAUDE.md` Commands, adjust for
   Windows: `client/build/windows/dj-app-client_artefacts/Debug/DJ App.exe`
   or similar under the artefact dir — confirm the exact path from the build
   output if this doesn't match).
   - Expected: a window titled "DJ App" opens, roughly 800×600, empty
     (blank/background-filled content — no controls yet, that's correct for
     M1).
5. Close the window via its close button (X).
   - Expected: the window closes and the process exits cleanly — no crash,
     no hang, no leftover process in Task Manager.
6. Run the test suite: `ctest --test-dir client/build/windows --output-on-failure`
   - Expected: all tests pass (same suites verified in the container —
     confirms no Windows/MSVC-specific behavior differs from the Linux
     build).

Report back: pass/fail per step, and the exact artefact path from step 4 if
it differs from `CLAUDE.md`'s documented path (update that doc if so).
