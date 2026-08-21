# M8 host checklist - second deck + mixer

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS Build Tools
(see `M3-host.md` if you need the reminder - plain "Developer Command Prompt" defaults to
32-bit detection). M8 adds a second, fully independent deck and a crossfader between them;
this is the first checklist where two decks play at once and the first to confirm the
crossfader - a purely local, never-synced control - behaves correctly both solo and
across a real multi-user connection. It also re-covers the exact scenario M7's checklist
caught a real bug in once already (a client that becomes controller must resync *both*
decks' position, not just one) - see Step 12, which exists specifically to catch a
regression of that fix.

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

Same toolchain as `M3-host.md`/`M5-host.md`/`M7-host.md`. You need `demo1.wav` and
`manifest.json` from those checklists already in place - if not, redo `M3-host.md`'s
"Test audio file" section first.

**New for M8: a second track.** The crossfade steps below need two *audibly different*
tracks playing on deck A and deck B at once, or you can't actually judge the fade by ear.
One checkout, one `manifest.json`, two entries - no second checkout needed (that's only
for the cross-instance missing-track scenario, which this checklist doesn't repeat).

1. Get a second `.wav`/`.aiff` file, at least ~20-30 seconds, **audibly distinct from
   `demo1.wav`** (a different pitch, a different phrase - you need to be able to tell by
   ear which deck you're hearing). Reuse `M3-host.md`'s PowerShell one-liner if you need to
   generate one, with a different sentence and output filename (e.g. `demo2.wav`).
2. Copy it into the track folder:
   ```
   copy "%USERPROFILE%\Desktop\demo2.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo2.wav"
   ```
3. Add it to `manifest.json` (edit the existing file, don't replace it) so it reads:
   ```json
   { "tracks": [
     { "id": "demo1", "title": "Demo One", "file": "demo1.wav", "bpm": 120 },
     { "id": "demo2", "title": "Demo Two", "file": "demo2.wav", "bpm": 120 }
   ] }
   ```
   Same validation rules as `M3-host.md`'s Step 4 - `id`/`file` must be valid, `file` must
   name a real file directly in `tracks/`.

## Steps

### Solo (one window, not connected) - deck B and the crossfader on their own

1. Update and build:
   ```
   cd C:\Users\nagi\Desktop\DJ-App
   git checkout claude
   git pull
   cmake -S client -B client/build/windows -G Ninja
   cmake --build client/build/windows
   ```
   - Expected: both `dj-app-client` and `dj-app-tests` build clean.
2. Run the test suite:
   ```
   ctest --test-dir client/build/windows --output-on-failure
   ```
   - Expected: **0 failures** (269 as of this session in the container; re-verify, don't
     trust a stale number).
3. Launch one instance of the client artefact (`"client\build\windows\dj-app-client_artefacts\Debug\DJ App.exe"`,
   falling back to the non-`Debug` path if that subfolder isn't there - see
   `DEVIATIONS.md`).
   - Expected: window opens with the track list on the left, **two** deck panes on the
     right (deck A on the left of that pair, deck B on the right - same controls each,
     Play/Pause, position slider, gain, rate, all three loop buttons, all disabled until a
     track is loaded), and a horizontal crossfader slider below both decks. Under the
     track list, a small button reading **`-> A`**. No crash on startup.
   - Expected: the crossfader starts at **dead center** and is enabled (nothing is
     controlling yet, so nothing blocks it - same "solo controls stay open" rule as every
     other control since M7).
4. Double-click "Demo One" in the track list.
   - Expected: it loads into **deck A only** - deck A's title becomes "Demo One" and its
     controls enable (same behavior as `M5-host.md`); deck B stays at "No track loaded",
     untouched. The `-> A` button reads its target: deck A is where double-click currently
     goes.
5. Click the **`-> A` / `-> B` toggle button**.
   - Expected: its label flips to **`-> B`** immediately. Nothing else changes - deck A
     keeps playing/whatever state it was in.
6. Double-click "Demo Two" in the track list.
   - Expected: it loads into **deck B only** this time - deck B's title becomes "Demo
     Two" and its controls enable; deck A's state (from Step 4) is completely
     undisturbed. This is the actual point of the toggle: confirm double-click now targets
     whichever deck the button names, not always the same one.
7. Click the toggle again to flip it back to **`-> A`**, and confirm the label updates.
   - Expected: symmetric with Step 5 - just proving the toggle isn't a one-way switch.
8. Start **both decks playing** (Play/Pause on deck A, then deck B).
   - Expected: both audible at once, roughly balanced in volume (the crossfader is
     centered) - you should be able to pick out both tracks playing simultaneously. This
     is the milestone's own "two tracks simultaneously" acceptance line.
9. **Crossfade, by ear.** Drag the crossfader slider slowly from center to the **far
   left**.
   - Expected: deck A gets louder relative to deck B as you drag, and at the **far left
     end, deck B goes completely silent** - only deck A is audible. Deck B's own
     Play/Pause state and position slider keep advancing on screen throughout (it's still
     "playing", just silenced by the fader, not paused).
10. Drag the crossfader to the **far right**.
    - Expected: the mirror image of Step 9 - deck A fades out, and at the far right end
      **deck A is completely silent**, only deck B audible.
11. Drag the crossfader back to **dead center**.
    - Expected: both audible again, similar balance to Step 8. (You may notice the
      combined loudness dip slightly right at center compared to either extreme - that's
      the deliberate equal-power curve, not a bug; there's no on-screen label explaining
      it, by design, so don't expect one.)

### Multi-user (two windows, connected) - mirroring and the position-resync fix

12. **Start the server in the container** (`cd server && npm start`), copy the printed
    room code, then launch a **second** instance of the client artefact. Connect both
    windows to that room with different display names, same as `M7-host.md` Steps 1-5.
    - Expected: same as `M7-host.md` Step 5 - both connect, peer lists populate, nobody
      controls yet, all controls (including both decks and the crossfader) stay enabled
      in both windows.
13. In window A, click **Claim Control**.
    - Expected: same as `M7-host.md` Step 6, extended to the crossfader - **window B's
      deck A controls, deck B controls, track list, the `-> A`/`-> B` toggle, *and the
      crossfader slider* all grey out.** The crossfader gates exactly like every other
      control now, even though its own effect is purely local - confirm it's actually
      disabled (can't be dragged), not just visually dim.
14. In window A: load "Demo One" to deck A, flip the toggle to `-> B`, load "Demo Two" to
    deck B, then play both.
    - Expected (window A): same as Step 8 above. Expected (window B, **within ~100 ms
      per deck**): **both** decks mirror - title, position, and playing state for deck A
      *and* deck B independently, not just one of them. This is the milestone's "both
      decks mirrored on observer" acceptance line: check deck B's mirroring specifically,
      not only deck A's, since deck A mirroring alone was already proven at M7.
15. **Position-resync check - both decks, this is the bug M8's review found and fixed.**
    With both decks still playing in window A, leave everything untouched for at least
    6-7 seconds (past the controller's 5 s resync interval), watching **window B's deck B**
    specifically.
    - Expected: no visible correction snap or audible glitch on deck B in window B - same
      "invisible on a healthy connection" expectation as `M7-host.md` Step 9, but this
      time confirming deck B's position keeps agreeing with window A's, not just deck A's.
      If deck B's position in window B visibly drifts away from window A's actual
      playback over these several seconds, that's the exact regression this checklist
      exists to catch - report it as a failure even though nothing crashes or looks
      broken at a glance.
16. **Crossfader stays local - confirm it does NOT sync.** In window A, drag its
    crossfader fully to one side.
    - Expected: window A's own audio balance shifts as in Steps 9-10, but **window B's
      crossfader slider does not move**, and window B's own local audio balance (between
      its copies of deck A and deck B) is completely unaffected by what window A's fader
      is doing. Each window's crossfader is its own independent, local setting - this is
      the deliberate design, not a missing feature, so confirm it explicitly rather than
      assuming silence means it's broken.
17. In window A, click **Release Control**, then in window B click **Claim Control**.
    - Expected: same as `M7-host.md` Steps 11-12 - window A's controls (including its
      deck B and crossfader) grey out, window B's controls (including its own crossfader)
      re-enable, and window B can now claim and drive both decks with window A mirroring
      correctly, symmetric to Steps 13-15 with the roles reversed.
18. Close both windows via their close buttons (X).
    - Expected: clean exit on both, no hang, no leftover process in Task Manager. Stop the
      server in the container (Ctrl+C).

Report back: pass/fail per step, and note anything wrong even if it isn't a crash -
especially any case where **deck B specifically** doesn't behave the same way deck A does
(Steps 4-7, 14-15), since deck B is what's new this milestone and the one place a
per-deck fan-out has already been found to silently miss a deck once (Step 15). Also flag
if the crossfader's disable state (Step 13) or its local-only independence (Step 16) don't
hold - those are the crossfader's two settled design guarantees, not open questions.
