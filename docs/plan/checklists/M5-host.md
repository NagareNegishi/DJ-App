# M5 host checklist — single-deck UI (real)

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS
Build Tools (see `M3-host.md` if you need the reminder on why — plain
"Developer Command Prompt" defaults to 32-bit detection). Confirms the real
`DeckComponent` (replacing the M3/M4 dev UI) behaves correctly with a real
audio device, and specifically exercises loop capture for the first time —
the engine side has been unit-tested since M3, but nothing before this
milestone made it reachable from the window.

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

Same toolchain and test-audio-file setup as `M3-host.md`. If
`client\assets\tracks\demo1.wav` and `manifest.json` are still present from
that checklist run, skip straight to Steps below — nothing about the track
files changes for M5. If not (e.g. a fresh checkout), redo `M3-host.md`'s
"Test audio file" section first, then come back here.

## Steps

1. Open the x64 Native Tools Command Prompt. Navigate to the repo and update:
   ```
   cd C:\Users\nagi\Desktop\DJ-App
   git checkout claude
   git pull
   ```
2. Configure and build:
   ```
   cmake -S client -B client/build/windows -G Ninja
   cmake --build client/build/windows
   ```
   - Expected: both `dj-app-client` and `dj-app-tests` build clean.
3. Run the test suite:
   ```
   ctest --test-dir client/build/windows --output-on-failure
   ```
   - Expected: **0 failures** (134 as of this session in the container;
     re-verify, don't trust a stale number).
4. Run the client artefact:
   ```
   "client\build\windows\dj-app-client_artefacts\Debug\DJ App.exe"
   ```
   (Quote the path. Fall back to
   `"client\build\windows\dj-app-client_artefacts\DJ App.exe"` if the
   `Debug` subfolder isn't there — see `DEVIATIONS.md`.)
   - Expected: window opens with the track list on the left and one deck's
     controls on the right — title "No track loaded", **Play/Pause,
     position slider, and all three loop buttons visibly disabled** (greyed
     out); gain and rate sliders enabled. No "Load Selected" button anymore
     — there's no button for it; loading is double-click only (next step).
     No crash on startup.
5. **Disabled-state check (M5 acceptance item):** before loading anything,
   try clicking Play/Pause and the loop buttons.
   - Expected: nothing happens (they're disabled, not just unresponsive-but-
     enabled — should visibly look greyed out, not clickable).
6. Double-click "Demo One" in the track list.
   - Expected: no crash; the deck's title updates to "Demo One"; the
     position slider's range becomes usable and the time readout shows
     `0:00 / <duration>`; Play/Pause, the position slider, and Loop In/Clear
     become enabled (Loop Out stays disabled — no loop armed yet).
7. Click **Play/Pause**.
   - Expected: audio plays audibly; button label flips to "Pause"; the
     position slider visibly advances on its own (no dragging) roughly once
     a frame, and the time readout counts up. Click again: audio pauses
     (silence, not reset), and **the slider stays exactly where it paused**
     — it must not snap backward to wherever it was when you pressed Play.
8. While playing, adjust **Gain** and then **Rate** (try 0.5 and 2.0).
   - Expected: volume/speed change audibly as before (rate changes pitch —
     expected, not a bug). **The position slider must keep advancing
     smoothly through each adjustment — no backward jump or freeze at the
     moment you touch Gain or Rate.** Double-click the Rate slider: it
     should snap back to 1.0 (the center-detent).
9. While playing, grab the position slider and drag it to a new spot, then
   release.
   - Expected: while your mouse button is held, the slider should track
     your drag, not fight it or jump around on its own; on release,
     playback audibly jumps to where you actually dropped it (not
     somewhere else).
10. **Loop capture — first time this is testable.** While playing, click
    **Loop In**.
    - Expected: the button's label changes (e.g. to "Cancel Loop In") and
      **Loop Out becomes enabled** (it was disabled before this). Let
      playback continue a few seconds, then click **Loop Out**.
    - Expected: playback now audibly loops between the two captured points
      (you'll hear it repeat); the Loop In button's label reverts to
      "Loop In"; Loop Out goes back to disabled until you arm a new capture.
      The position slider/time readout should visibly wrap back to the
      loop's start each time it passes the loop's end, in sync with what
      you hear.
11. Click **Clear Loop**.
    - Expected: looping stops, playback continues straight through instead
      of wrapping.
12. **While paused** (position frozen, so this is reproducible at human
    speed — attempting it while playing races the advancing position and
    can't reliably land out at-or-before in), click **Loop In**, then
    immediately **Loop Out** without moving/waiting.
    - Expected: no loop applied (log line only, check the console/log if
      curious) — playback keeps going straight through when you press Play,
      doesn't get stuck in a zero-length loop. Loop In's label reverts to
      normal.
13. Let the track play to its end without touching any control.
    - Expected: playback stops on its own at end-of-track; the position
      slider settles at the end rather than freezing mid-track or reading
      the wrong number. Click **Play/Pause** again.
    - Expected: it restarts from 0 (not stuck trying to resume past the
      end) — same self-stop-recovery behavior as M3/M4, now reached through
      the real deck UI.
14. Double-click a different track (or the same one again) while the first
    is still loaded/playing.
    - Expected: no crash; the deck switches to the new track, position
      resets to `0:00`, and any active loop from the previous track is gone
      (a fresh load always clears the loop).
15. Close the window via its close button (X).
    - Expected: clean exit, no hang, no leftover process in Task Manager.

Report back: pass/fail per step, which artefact path actually existed in
Step 4, and note anything audibly or visually wrong — especially any
backward jump/snap of the position slider (Steps 7-9 exist specifically to
catch that regressing) — even if it isn't a crash.
