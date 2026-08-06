# M1 host checklist — walking skeleton window

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS
Build Tools 2022 (not the plain "Developer Command Prompt," which defaults to
32-bit detection — see `docs/plan/DEVIATIONS.md`'s 2026-08-05 JUCE-install-path
entry). Confirms the JUCE window builds and runs on the platform that actually
plays audio — the container only proves it compiles.

## Prerequisites (one-time, skip if already installed)

1. VS Build Tools 2022, "Desktop development with C++" workload. Gives
   `cl.exe`, the Windows SDK, MSBuild, and Ninja (`docs/setup.md` →
   Windows Host Toolchain).
2. JUCE 8.0.12 built and installed, from the x64 Native Tools Command Prompt
   **opened via "Run as administrator"** (`docs/setup.md` → Windows
   developer setup):
   ```
   git clone --depth=1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git C:\JUCE
   cmake -S C:\JUCE -B C:\JUCE\build -G Ninja
   cmake --build C:\JUCE\build
   cmake --install C:\JUCE\build
   ```
   Elevation is required for this step specifically: JUCE's own
   `project(JUCE ...)` name means an unset `CMAKE_INSTALL_PREFIX` defaults
   to `C:\Program Files\JUCE`, which a standard (non-admin) prompt cannot
   write to — `cmake --install` fails with an access-denied error otherwise.
   No elevation is needed for steps 1-3 below (they only read from
   `C:\Program Files\JUCE`, they don't write there). No path variable to set
   afterward, either: CMake's `find_package(... CONFIG)` search on Windows
   checks `Program Files\<PackageName>*` automatically, so
   `find_package(JUCE CONFIG REQUIRED)` finds it with no `CMAKE_PREFIX_PATH`
   needed — confirmed against CMake's own `find_package` docs, not assumed.

## Steps

1. From the x64 Native Tools Command Prompt at the repo root: `git checkout claude`
   then `git pull`.
   - Expected: `client/src/Main.cpp`, `client/src/app/MainWindow.*`,
     `client/src/app/MainComponent.*` are present (not the old placeholder
     console app).
2. Configure: `cmake -S client -B client/build/windows -G Ninja`
   - Expected: configures without error; reports JUCE found via
     `find_package`.
3. Build: `cmake --build client/build/windows`
   - Expected: `dj-app-client` and `dj-app-tests` both build clean, no
     errors. Warnings are worth flagging back but not blocking.
4. Run the client artefact: `client\build\windows\dj-app-client_artefacts\DJ App.exe`
   - This path is not a guess: both platforms configure with `-G Ninja`
     (single-config), and the container's actual artefact dir
     (`client/build/linux/dj-app-client_artefacts/DJ App`, verified against
     the built tree) has no `Debug`/`Release` subfolder — JUCE only nests a
     build-type folder for multi-config generators, which Ninja isn't. If
     the real path on Windows differs from this, that's a deviation to
     report, not an expected variant.
   - Expected: a window titled "DJ App" opens, roughly 800×600, empty
     (blank/background-filled content — no controls yet, that's correct for
     M1).
5. Close the window via its close button (X).
   - Expected: the window closes and the process exits cleanly — no crash,
     no hang, no leftover process in Task Manager.
6. Run the test suite: `ctest --test-dir client/build/windows --output-on-failure`
   - Expected: all tests pass, same suite count as the container (38/38 —
     confirms no Windows/MSVC-specific behavior differs from the Linux
     build).

Report back: pass/fail per step. If step 4's actual path differs from the
one above, report it as a deviation (something changed the generator's
output layout) rather than a substitution.
