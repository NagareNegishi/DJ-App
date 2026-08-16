# M7 host checklist — multi-user sync on one deck

Run on the Windows host, from the **x64 Native Tools Command Prompt** for VS Build Tools
(see `M3-host.md` if you need the reminder — plain "Developer Command Prompt" defaults to
32-bit detection). This is the product's core promise: two app instances, one controller,
shared state, audible on both, over a real WebSocket connection. Everything up to this
point has been single-user; this is the first checklist that needs the server running and
two client windows open at once.

This repo is checked out at `C:\Users\nagi\Desktop\DJ-App` on this host.

## Prerequisites

Same toolchain and test-audio-file setup as `M3-host.md`/`M5-host.md` — if
`client\assets\tracks\demo1.wav` and `manifest.json` are already present, skip to Steps.
You'll need **two copies of the same audio files** reachable by each client instance —
either two full checkouts of the repo, or one checkout with the client run twice against
the same `client\assets\tracks\` (both instances read the same manifest; that's fine and
matches "each rendering audio locally from their own copy of the files" — it's still each
instance's own independent decode/playback, just pointed at the same files on disk).

The server runs **in the container**, not on the host — this checklist is the first one
that needs both sides running simultaneously.

## Steps

1. **Start the server, in the container** (a separate terminal from the client work below):
   ```
   cd server && npm start
   ```
   - Expected: a startup line naming the bind address (`127.0.0.1:8765` by default) and a
     `room code: <code>` line. **Copy that room code** — you'll need it for both client
     instances below. Leave this running for the rest of the checklist.
2. On the Windows host, update and build:
   ```
   cd C:\Users\nagi\Desktop\DJ-App
   git checkout claude
   git pull
   cmake -S client -B client/build/windows -G Ninja
   cmake --build client/build/windows
   ```
   - Expected: both `dj-app-client` and `dj-app-tests` build clean.
3. Run the test suite:
   ```
   ctest --test-dir client/build/windows --output-on-failure
   ```
   - Expected: **0 failures** (218 as of this session in the container; re-verify, don't
     trust a stale number).
4. Launch **two** instances of the client artefact (`"client\build\windows\dj-app-client_artefacts\Debug\DJ App.exe"`,
   falling back to the non-`Debug` path if that subfolder isn't there — see
   `DEVIATIONS.md`). Arrange the two windows side by side so you can watch both at once.
   - Expected: both open cleanly, no crash, each showing the connect panel above the track
     list and deck controls, same as always. Deck controls start **enabled** on both (solo
     local playback before connecting must still work, unchanged from M5/M6).
5. **Connect both.** In each window's connect panel: the URL field should already read
   `ws://127.0.0.1:8765` (the documented default) — leave it. Enter the room code you
   copied in Step 1 into the room field, and a different display name in each window (e.g.
   "nagare" / "aki"). Click **Connect** in both.
   - Expected: the status area in both windows changes to something indicating a live
     connection (not "Connecting…" or an error) within a second or two. Each window's peer
     list shows the *other* window's name and role ("observer"). Neither has claimed
     control yet, so **Claim Control** should be enabled and deck controls should already
     be enabled in both (nobody controls yet, so nothing to be blocked by) — see the note
     on Step 8 for what changes once someone does claim.
   - **If connection fails / times out**: this devcontainer forwards the container's port
     8765 to the host automatically via VS Code's Dev Containers port forwarding, so
     `127.0.0.1:8765` from the host should reach the container's server. If it doesn't,
     check VS Code's "Ports" panel for an active forward on 8765; if there genuinely isn't
     one, that's a real gap worth recording in `DEVIATIONS.md` rather than working around
     silently (e.g. don't just set `HOST=0.0.0.0` in the container and move on without
     noting it — that changes the server's security posture, `06-security.md` §Server
     rule 3).
6. In window A, click **Claim Control**.
   - Expected: window A's button now reads "Release Control"; window A's deck controls are
     (still) fully enabled. **Window B's deck controls (Play/Pause, position slider, gain,
     rate, all three loop buttons, and the track list) all visibly grey out** — this is the
     first time role-based disabling has ever been reachable, since it needs a real peer to
     exist. Window B's peer list should now show window A's role as "controller", and
     window B's own claim button should read "Claim Control" still, disabled or not
     depending on whether you built it that way — the important thing is window B cannot
     act as controller right now.
7. In window A, double-click a track to load it, then click **Play/Pause**.
   - Expected (window A): audible playback, same as solo mode always was.
   - Expected (window B, **within about 100 ms**): the deck's title/position/play state all
     mirror window A's — window B should also be audibly playing the same track from
     roughly the same position, entirely from its own local copy of the file (no audio
     data crossed the network — only the state deltas did). Window B's controls remain
     disabled throughout; you cannot interact with them.
8. While still playing in window A, exercise **seek, gain, rate, and loop** one at a time
   (drag the position slider, move gain, move rate, capture a loop in/out, then clear it).
   - Expected: each change mirrors audibly and visually on window B within about 100 ms,
     same as Step 7. Confirm specifically that a **loop captured on A wraps audibly on B
     too**, and that clearing it on A stops the wrap on B.
9. **Drift resync check.** Let the track play, untouched, for at least 6-7 seconds (past
   the controller's 5 s position resync interval) without touching anything on either
   window.
   - Expected: no visible correction snap or audible glitch on window B — the resync is
     supposed to be invisible when nothing has actually drifted; you're confirming it
     doesn't do anything disruptive on a healthy connection, not that you can see it firing.
10. In window B, try clicking anywhere on its (disabled) deck controls or double-clicking a
    track in its list.
    - Expected: nothing happens — window A's playback state is completely unaffected.
11. In window A, click **Release Control**.
    - Expected: window A's button reverts to "Claim Control". **Window B's deck controls
      re-enable** (grey-out lifts) even though nobody has claimed control yet — "no
      controller" means either side is free to claim, not that B stays locked out.
      Window A's own state keeps playing undisturbed (releasing control doesn't stop
      playback, it just frees who's allowed to change it next).
12. In window B, click **Claim Control**, then load and play a *different* track than
    whatever A had loaded.
    - Expected: control transfers correctly — window A's controls now grey out and mirror
      window B's new track/playback within ~100 ms, symmetric to Steps 6-8 with the roles
      reversed. This confirms control isn't sticky to whichever window claimed it first.
13. **Controller disconnect frees control.** With window B still controller, close window
    B entirely (its window's close button, not a graceful disconnect click).
    - Expected: window A's peer list loses window B's entry (no leftover "ghost" peer).
      Window A's own deck controls become enabled again (control is free). Window A's
      audio keeps playing undisturbed — losing the controller doesn't stop what's already
      playing, it just means nobody currently has the right to change it until someone
      claims. Window A should **not** show any stray role-change notice for window B's
      departed id — just the peer leaving.
14. **Missing track.** Restart the window you just closed (relaunch the exe), connect it to
    the same room with a different display name, but this time **rename or remove one
    track's file** from that instance's `client\assets\tracks\` before connecting (or point
    this instance at a `client\assets\tracks\` directory whose manifest is missing an entry
    the other side has). Have window A claim control and load/play the track this window
    doesn't have.
    - Expected: this window shows a "missing track" indication (whatever text/UI the
      implementation surfaces — confirm it's legible, not a crash or silent blank deck) and
      stays silent (no audio, no crash) while continuing to receive and reflect every other
      piece of state (gain/rate/position keep updating even though there's nothing audible
      to apply them to). Restore the file afterward if you want to keep using this checkout
      for further testing.
15. **Reconnect after a drop.** With both windows connected, stop the server process in the
    container (Ctrl+C on the `npm start` terminal).
    - Expected: both windows' status areas show a disconnected state within ~30 s (the
      keepalive/reap window) or immediately if the socket closes cleanly; deck controls
      return to the "solo, fully enabled" state on both, same as Step 4, since there's no
      longer a room to be an observer in. Restart the server (`npm start` again — note this
      generates a **new** room code unless you set `DJ_ROOM_CODE`), enter the new room code
      in both windows, and click **Connect** again (the same button — this app has no
      separate reconnect button; clicking Connect while disconnected *is* the reconnect
      path, per `07-milestones.md`'s M7 task list).
    - Expected: both reconnect cleanly and re-establish a working session — claim control
      on one, confirm mirroring still works, same as Steps 6-8.
16. Close both windows via their close buttons (X).
    - Expected: clean exit on both, no hang, no leftover process in Task Manager. Stop the
      server in the container (Ctrl+C) if you haven't already.

Report back: pass/fail per step, the actual mirror latency if it felt slower than ~100 ms
on anything, and note anything wrong even if it isn't a crash — especially any case where
window B's controls *don't* grey out when they should (Steps 6, 12) or *don't* re-enable
when they should (Steps 11, 13, 15), since that's the one behavior this milestone adds that
has never been reachable or checklist-verified before.
