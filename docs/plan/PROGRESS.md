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
- 2026-08-21 · **M7 — Multi-user sync on one deck** · container green: `client/build/linux`
  builds clean, `ctest` 239/239; `server` suite 669/669. Host checklist
  (`checklists/M7-host.md`) confirmed on Windows, all 16 steps pass: claim/release control
  gates the non-controller's deck correctly, load/play/seek/gain/rate/loop all mirror to
  the observer within ~100 ms from its own local file copy, the 5 s drift resync is
  invisible on a healthy connection, controller disconnect frees control without a ghost
  peer, a track missing from one instance's `assets/tracks` shows a legible
  `missing track: <id>` label and stays silent while still reflecting other state, and
  reconnect-after-server-drop works with no separate reconnect button. Five bugs found and
  fixed during the host pass, each recorded in `DEVIATIONS.md` under 2026-08-21: a missing
  `USE_ZLIB OFF` build flag for IXWebSocket, an em dash in `TEST_CASE` names breaking
  CTest's Windows filter, a track double-click that did nothing before ever connecting, a
  server origin check that rejected the client's own handshake, and claiming control not
  resyncing the room to the new controller's already-playing state.
- 2026-08-21 · **M8 — Second deck + mixer** · container green: `client/build/linux`
  builds clean, `ctest` 270/270; `server` suite 673/673. Host checklist
  (`checklists/M8-host.md`) confirmed on Windows, all 18 steps pass: deck B loads,
  plays, and mirrors to an observer exactly like deck A (the `-> A`/`-> B` load-target
  toggle switches which deck a track-list double-click targets); both decks audible
  together with the crossfader centered; the equal-power crossfade is silent at each
  far end and correct at center; the crossfader gates off for a non-controller like
  every other control while staying local-only (never synced) per this milestone's
  recorded protocol decision (`DEVIATIONS.md`, 2026-08-21); the 5 s drift resync stays
  invisible on deck B specifically, not just deck A, confirming the fix below. Landed
  across two build-log sessions
  (`build-orchestration/build-log/2026-08-21-m8-second-deck-mixer.md`,
  `...-m8-review-fixes.md`): two whitebox-pinned bugs fixed exactly as specified
  (`EngineAdapter::attachCrossfader` double-registering its listener on reattach,
  `CrossfaderState::setPosition` double-notifying on a reentrant call), then a review
  layer (five advisers) found one real bug - `MainComponent::applyRoleToUI` only set
  the new controller's role on deck A's `PositionClock`, never deck B's, so deck B's
  periodic resync silently never fired for any observer - fixed via a new
  `state/DeckPositionClocks` wrapper (covered by `DeckPositionClocksTest.cpp`) that
  fans one `setRole` call to both decks instead of `MainComponent` hand-duplicating
  the call site. A sixth bug surfaced by this session's own host-checklist Step 3 (not
  the review layer): `DeckComponent`'s gain and rate sliders stayed enabled with no
  track loaded, the only two of seven deck widgets that skipped the `hasTrack` check
  every sibling control already had. Fixed by extracting the shared decision into
  `model/ControlGating.h::deckControlEnabled`, covered by `ControlGatingTest.cpp`, so
  the seven call sites can't diverge again - the same fix shape as M7's
  `controlsEnabledLocally` extraction. `docs/plan/07-milestones.md` gained a new M9
  (track repeat/auto-replay, closing a gap from the original plan) inserted right
  after this milestone, renumbering the former M9 (beat alignment) to M10 throughout
  the plan docs.
- 2026-08-21 · **M9 — Track repeat (auto-replay)** · container green: `client/build/linux`
  builds clean, `ctest` 294/294; `server` suite 682/682. Protocol bumped to v2 (`repeat`
  field). Host checklist (`checklists/M9-host.md`) confirmed on Windows, all 17 steps pass:
  repeat defaults on, loops seamlessly at end-of-track, the toggle switches the pre-M9
  stop-at-end behavior on and off, an armed loop takes priority over repeat, and the toggle
  mirrors to an observer within ~100 ms. Two review passes during the build caught the same
  bug shape four times - `repeat` missing from every full-`PlaybackState`-copy path
  (`StateManager::applyDelta`'s merge, snapshot apply, claim-control resync,
  `SyncPublisher::mergeField`) while every incremental delta path already carried it - each
  fixed the same way. The host checklist then caught a fifth, UI-only case: an observer's
  repeat icon never repainted, since `juce::Button::setToggleState()` no-ops while disabled
  and the button is disabled on every observer refresh; fixed by force-enabling around the
  call (`DEVIATIONS.md`, 2026-08-21). No automated test for that last one - `dj-app-tests`
  excludes `juce_gui_basics` by design, and widget behavior stays host-checklist-only per
  `05-testing.md`.
- 2026-08-24 · **M10 — Beat alignment (time-stretch, pitch, beat detection, sync)** ·
  container green: `client/build/linux` builds clean, `ctest` 360/360 (up from 294).
  No server-side changes this milestone - `server` suite unchanged at 682/682. Six units
  (A-F: `SignalsmithTimeStretcher`/`TimeStretcher` interface, `QmDspBeatDetector`/
  `BeatDetector` interface, `EngineAdapter` beat-grid caching, `BufferPlaybackSource`
  real time-stretch wiring, and the sync button + pitch slider UI) landed a real
  time-stretcher (rate now changes speed without pitch), a pitch slider, real
  per-track beat detection, and a per-deck sync button. qm-dsp (GPL-2.0-or-later) is
  the one GPL dependency beyond JUCE, recorded per `CLAUDE.md`'s license rule
  (`decisions.md` Section 6.1). A post-implementation review layer
  (`correctness-adviser`, `design-adviser`, `simplicity-adviser`, `legal-adviser`)
  found two real bugs in the sync button: `computeBeatSync` wrapped both decks'
  phase using the calling deck's own beat interval instead of each deck's own, wrong
  whenever the two BPMs differ (the sync button's entire purpose); and the other
  deck's position came from a Play-button resume heuristic that substitutes 0.0 at
  end-of-track, wrong for a phase reference. Both fixed together (the phase fix needs
  the other deck's live rate as a new input, the position fix needed the same
  provider redesign), plus a batch of five smaller review findings (`EngineAdapter`'s
  cache keyed on a dangling-prone buffer pointer instead of `trackId`, a stretcher
  pull-head reconstruction that recomputed rather than cached the previous block's
  latency compensation, an unhandled zero-length render block, a stretcher-reset
  threshold that didn't actually exclude the sync button's own phase nudge as
  documented, and an end-of-track resume epsilon too narrow for a real stretcher's
  latency) - all recorded in `DEVIATIONS.md`. The unused `signalsmith_linear`
  dependency was also removed (never linked into anything, per the 2026-08-22
  finding that Signalsmith Stretch doesn't need it), and `THIRD_PARTY_NOTICES.md`
  gained qm-dsp and Signalsmith Stretch entries. Two known gaps deliberately
  deferred to a future milestone, both recorded in `DEVIATIONS.md`: the sync
  button's minimal UX (no BPM readout, silent no-op on failed detection, no visual
  distinction between failure states), and whether `EngineAdapter::handleDelta`'s
  synchronous beat analysis can stall the message thread on a long track - a new
  host-checklist step exists to gather that evidence but has not been run yet.
  **Host checklist (`checklists/M10-host.md`) has not been run on the Windows host
  as of this entry** - milestone-discipline sign-off is still pending that manual
  pass; this line records the container-side state only.
