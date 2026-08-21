# M9 host checklist - track repeat (auto-replay)

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS Build Tools
(see `M3-host.md` if you need the reminder). M9 makes a track loop back to its start on
reaching its natural end instead of stopping, by default, with a per-deck toggle to turn
that off. This is the first checklist where you need to actually let a track play to its
end and watch what happens next - budget a short test track so that isn't a long wait.

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

Same toolchain and track setup as `M3-host.md`/`M8-host.md` - `demo1.wav`/`demo2.wav` and
`manifest.json` should already be in place.

**New for M9: a short track.** Waiting for a 20-30 second file to reach its end on every
step below would make this checklist slow. Generate a short one (5-8 seconds is enough to
hear a clean loop-back) and add it alongside the existing entries:

1. Generate or trim a short, audibly distinct `.wav` (reuse `M3-host.md`'s PowerShell
   one-liner with a short duration, e.g. `demo-short.wav`).
2. Copy it into the track folder:
   ```
   copy "%USERPROFILE%\Desktop\demo-short.wav" "C:\Users\nagi\Desktop\DJ-App\client\assets\tracks\demo-short.wav"
   ```
3. Add it to `manifest.json` alongside the existing entries:
   ```json
   { "id": "demo-short", "title": "Demo Short", "file": "demo-short.wav", "bpm": 120 }
   ```

## Steps

### Solo (one window, not connected) - default repeat, the toggle, and the loop interaction

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
   - Expected: **0 failures** (re-verify the current count in the container, don't trust a
     stale number from a prior milestone's checklist).
3. Launch the client, double-click "Demo Short" to load it onto deck A.
   - Expected: same as every prior milestone - title, controls enable. Note the new
     **repeat toggle button** next to the loop buttons: a small circular-arrow icon,
     distinct from every other control so far (all text-only until now). It should render
     as **on/enabled-looking** by default (repeat defaults to `true`) once a track is
     loaded - confirm its visual "on" state is legible, not just guessable.
4. Click Play and let the track run **past its natural end**, watching and listening
   without touching anything.
   - Expected: **the track loops** - audio restarts cleanly from the beginning with no
     stutter, click, or dropout at the seam, and keeps playing (Play/Pause button still
     reads "Pause", not reverting to "Play"). The position slider and time readout should
     **wrap back toward zero** rather than pinning at the far right or counting past the
     track's actual length - this is the default, unattended behavior the milestone adds.
5. Let it loop **two or three times** in a row, unattended.
   - Expected: keeps repeating cleanly every time, no degradation, no eventual stop.
6. Click the repeat toggle to turn it **off**.
   - Expected: its visual state changes to reflect "off" immediately.
7. Let the track run to its end again.
   - Expected: **the pre-M9 behavior returns** - the track stops at its natural end
     (Play/Pause reverts to "Play", position holds at the end, no loop-back). This is the
     "toggling repeat off lets a track stop normally at end" acceptance line.
8. Click the repeat toggle back **on**, and confirm the track resumes looping the same way
   as Steps 4-5 (press Play again first if it had stopped in Step 7).
9. **Loop + repeat interaction.** With repeat on, arm a small loop well inside the track
   (Loop In near the start, Loop Out a moment later, both comfortably before the track's
   actual end) and let playback run.
   - Expected: the loop takes priority - audio cycles within the small loop region and
     never reaches the track's real end at all, regardless of repeat being on. Clear the
     loop (Clear Loop button) and confirm the track then plays through normally to its end
     and repeats again per Steps 4-5.

### Multi-user (two windows, connected) - the toggle mirrors, and resync carries repeat

10. Start the server in the container (`cd server && npm start`), copy the room code,
    launch a second client instance, connect both windows with different display names -
    same as `M7-host.md` Steps 1-5.
    - Expected: both connect, peer lists populate, nobody controls yet, repeat toggle
      stays enabled in both windows (same "solo controls stay open" rule as every other
      control).
11. In window A, click **Claim Control**, load "Demo Short" to deck A, and confirm repeat
    reads "on" by default in both windows.
12. In window A, click the repeat toggle to turn it **off**.
    - Expected: **within ~100 ms**, window B's repeat toggle for deck A also flips to
      "off" - same mirroring latency as every other synced control (gain, rate, loop).
      Confirm window B's toggle is disabled/greyed (it's an observer), not just showing
      the wrong state.
13. Play the track in window A and let it run to its end, watching **both windows**.
    - Expected: both windows show the track stopping at its natural end (repeat is off),
      in sync with each other - no divergence between the controller's and observer's
      view of whether it stopped.
14. In window A, toggle repeat back **on**, press Play, and let it loop once, watching
    both windows.
    - Expected: both windows show the loop-back within the same ~100 ms mirroring window,
      audio only audible in window A (window B is silent per the existing missing-audio
      design - only local state/UI mirrors, never audio).
15. **Snapshot carries repeat - the fix this session's review found.** With repeat off on
    deck A in window A (toggle it off if it isn't already), close window B, then launch a
    **fresh third window** and connect it to the same room.
    - Expected: the new window's deck A repeat toggle reads **off** immediately on join,
      matching window A's actual state - not stuck at the `true` default. This confirms a
      newly-joining client's snapshot correctly carries the current repeat value.
16. **Claim-control resync carries repeat.** In the fresh window from Step 15 (still
    observer), toggle nothing; in window A, click **Release Control**; in the fresh
    window, click **Claim Control**.
    - Expected: after claiming, the room's repeat value for deck A stays correctly "off"
      (matching what window A had set) rather than reverting to the `true` default -
      confirms the full-resync-on-claim path also carries repeat correctly.
17. Close all windows via their close buttons (X).
    - Expected: clean exit, no hang, no leftover process in Task Manager. Stop the server
      in the container (Ctrl+C).

Report back: pass/fail per step, and specifically flag anything in Steps 15-16 that doesn't
hold - those two exist because a review this session found `repeat` missing from two of the
three full-state-copy paths (snapshot apply, claim-control resync) before the fix landed,
and a passing Step 4-5/12-14 alone would not have caught either gap.
