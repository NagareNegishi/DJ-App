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
- 2026-08-06 · **M3 — AudioEngine (single deck)** · container green: `client/build/linux`
  builds clean, `ctest` 87/87 (`BufferPlaybackSource` offline-render suite added).
  Host checklist (`checklists/M3-host.md`) confirmed on Windows (VS Build Tools 2026):
  load/play/pause/seek/gain/rate all audible and correct, rate 0.5x/2.0x pitch-shifts
  as expected, end-of-track self-stops, clean window close. Two bugs found and fixed
  during the host pass: `1db3762` removed a Windows-broken test setup (a literal
  backslash in a filename is a path separator there, not a character); `73075b9`
  fixed the dev-UI's Play/Pause handler getting stuck after a natural end-of-track
  stop instead of restarting. `Debug` artefact-subfolder deviation from M1 confirmed
  still present, no new deviations.
- 2026-08-06 · **M4 — StateManager** · container green: `client/build/linux` builds
  clean, `ctest` 134/134 (`StateManager`, `EngineAdapter`, `SyncTransport`/
  `NullTransport`, `SyncPublisher` suites added; a whitebox pass added 14 more,
  including a regression test for a real re-entrancy bug). Host checklist
  (`checklists/M3-host.md`, re-run per M4's acceptance criteria — "behavior on host
  identical to M3") confirmed on Windows: load/play/pause/seek/gain/rate all correct,
  no regressions from routing the dev UI through `StateManager.applyDelta`. Two bugs
  found and fixed during the review pass, both inside `b0d6d38`/`de7d41a`: a
  stale-position bug on resume-after-pause (`StateManager`'s stored-position
  injection goes stale once `PositionClock` starts advancing playback, so
  `MainComponent::togglePlayPause` now supplies live engine position on resume
  instead of relying on injection), and a segfault in
  `StateManager::applyDelta`'s notification loop when a listener unsubscribes
  itself mid-notification (fixed by walking listener tokens via fresh lookups
  instead of holding a `std::map` iterator across a callback). `Debug`
  artefact-subfolder deviation from M1 reconfirmed present; host checklists
  corrected to specify the x64 Native Tools Command Prompt (plain "Developer
  Command Prompt" defaults to 32-bit detection). No new deviations.
- 2026-08-07 · **M5 — Single-deck UI** · container green: `client/build/linux`
  builds clean, `ctest` 153/153 (`EngineAdapter` self-stop and `LoopWrap`
  regression suites added, 19 new tests). Host checklist
  (`checklists/M5-host.md`) confirmed on Windows, all 15 steps pass: real
  `DeckComponent` load/play/pause/seek/gain/rate correct, disabled-state
  gating correct, loop capture/wrap/clear correct, clean window close. Two
  bugs found on an earlier pass this session and fixed before the final
  confirmed run: a self-stop label desync (`playing` state going stale after
  the engine stopped itself, `33803bb`) and a loop-arm ratchet that could
  only narrow on re-arm, never widen or relocate (`67db4a7`, plus a
  track-change edge case caught in review, `d76259a`). Checklist step 12
  corrected: capturing a zero-length loop can't be reproduced at human speed
  while playing (position races the click), so the step now specifies doing
  it while paused. `Debug` artefact-subfolder deviation from M1 reconfirmed
  present. No new deviations.
- 2026-08-15 · **M6 — Sync server and protocol fixtures** · container green:
  `server` suite 667/667 (up from 631), `client/build/linux` builds clean,
  `ctest` 164/164. CI green on `084b37c` — the first run the server job has ever
  had, since nothing from M6 had been pushed until now. No host checklist: M6
  touches no audio and no GUI, so there is nothing a Windows host can tell us
  that the container cannot. Landed in two sessions. The first built the server
  and the shared fixture corpus (`build-log/2026-08-14-m6-server-protocol.md`);
  the second ran the review it had not had (`build-log/2026-08-15-m6-review.md`),
  a whitebox pass plus five advisers, 22 findings, 15 commits. One was high:
  every policy close in the server was advisory, because `ws.close()` does not
  stop the receiver and nothing checked whether a connection had been ejected,
  so a peer closed for flooding kept mutating canonical state and broadcasting
  to everyone else for up to 30 s (`a0460e2`). The same root cause let 32 silent
  sockets hold every pre-hello slot and make the room unjoinable. Also fixed: a
  repair-snapshot dedup that left a client permanently diverged after losing
  control (`3fdc51b`), a ban clock that closed provably conforming clients
  (`514c771`), and five smaller defects. Of the two paths M6 shipped with no
  test, the backpressure ceiling is now covered and the heartbeat reaper stays
  exempt per `05-testing.md`. Four plan documents were corrected against the
  code they specify, and one protocol amendment is recorded in `DEVIATIONS.md`
  (one `rate-limited` error per deficit episode, not per dropped frame; version
  stays at 1). Known gaps carried into M7, all recorded in the review build-log:
  the room code is still the only membership gate and handshakes are still
  unthrottled, and `02-protocol.md` still does not pin the `roleChanged` payload
  or ordering on controller disconnect.
