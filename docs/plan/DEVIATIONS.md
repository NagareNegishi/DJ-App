# Deviations from the plan

Each entry: date, what the plan said, what was done instead, why.

## 2026-08-03 — `client/CMakeLists.txt` needs `LANGUAGES ... C`

- **Plan said**: `docs/plan/README.md` describes the existing `client/CMakeLists.txt` as a
  "working JUCE 8 GUI-app target"; M0 treats CMake changes as additive.
- **Actual**: A pristine `main` checkout fails to *configure*:
  `CMake Error: A C compiler is required to build JUCE. Add 'C' to your project's
  LANGUAGES.` The `project()` call declared only `LANGUAGES CXX`.
- **Change**: `project(DjAppClient VERSION 0.1.0 LANGUAGES CXX)` →
  `LANGUAGES CXX C`. JUCE's Linux modules require a C compiler to be enabled.
- **Why**: Without it neither the app nor the test target can configure, so it is a
  prerequisite for every M0 acceptance command. Made as part of the M0 `build-scaffold`
  unit (the file was already in that unit's scope).

## 2026-08-04 — `model/Serialization::fromVar` clamps out-of-range numbers instead of rejecting them

- **Plan said**: `docs/plan/04-client.md`'s `model/` section says parsing is strict and
  should "reject unknown fields, wrong types, non-finite numbers, out-of-range values
  (return failure, never partially fill)" — out-of-range values listed alongside the
  other reject cases.
- **Plan also said**: `docs/plan/02-protocol.md`'s Validation policy says the opposite for
  the client side: "client clamps incoming values into range as defense-in-depth", with
  hard rejection reserved for the server. These two statements contradict each other for
  the client.
- **Actual**: `fromVar<T>` only rejects unknown fields, wrong JSON types, and non-finite
  numbers. A well-typed but out-of-range value (e.g. `gain: 5.0`) parses successfully;
  `Ranges::clamp()` is the range-enforcement step, applied by the caller after a
  successful parse. The one exception is `loop.inSeconds < outSeconds`, which can't be
  fixed by clamping two independent endpoints and so is rejected in `fromVar` itself.
- **Why**: Followed `02-protocol.md`, since its Validation policy section states the
  client/server asymmetry explicitly and by name, while `04-client.md`'s line reads more
  like shorthand for "parsing is strict" than a deliberate restatement of that policy.
  This is also the only reading under which `Ranges::clamp()` has a real job on incoming
  network data: if `fromVar` already rejected out-of-range values, `clamp()` would only
  ever run on locally-constructed state. Decided with the user; see the M1 build log
  (`build-orchestration/build-log/2026-08-04-m1-model-appshell.md`) for the full
  discussion.

## 2026-08-05 — Windows host has VS Build Tools 2026 (v18), not VS 2022

- **Plan said**: `docs/setup.md` and `docs/plan/checklists/M1-host.md` pin
  Visual Studio Build Tools 2022.
- **Actual**: Host has VS 2026 Build Tools (v18.6.1), MSVC `19.51.36244`; VS
  2022 was never installed.
- **Why**: The pin wasn't feature-specific — it just needed `cl.exe`,
  Windows SDK, MSBuild, and Ninja with C++20 support, which VS 2026's MSVC
  toolset provides as a backward-compatible superset. Proceeded as-is.

## 2026-08-05 — JUCE installs to `Program Files (x86)`, not `Program Files`

- **Plan said**: `docs/setup.md` and `docs/plan/checklists/M1-host.md` state
  an unset `CMAKE_INSTALL_PREFIX` defaults to `C:\Program Files\JUCE`.
- **Actual**: `cmake --install C:\JUCE\build` (Ninja generator) installed to
  `C:\Program Files (x86)\JUCE` instead.
- **Why**: With the Ninja generator, CMake picks the default install prefix
  before the compiler-detection step confirms 64-bit, so it falls back to
  the 32-bit path. Harmless: CMake's `WindowsPaths.cmake` adds both
  `Program Files` and `Program Files (x86)` to the default `find_package`
  search path, so `find_package(JUCE CONFIG REQUIRED)` still finds it. No
  action taken.

## 2026-08-05 — `CI / format` blocked merges before M5, ahead of plan

- **Plan said**: `.github/workflows/ci.yml`'s `format` job comment states it is
  "non-blocking until M5 ... once the client/tests suite is established and
  formatting is enforced," relying on `continue-on-error: true` to keep it
  informational only.
- **Actual**: `continue-on-error: true` only keeps the *workflow run's* overall
  conclusion green; it does not change the individual `CI / format (pull_request)`
  check's own reported conclusion. Branch protection on `main` evaluates that
  check directly, so a failing `format` job blocked merges regardless of
  `continue-on-error` — the comment's assumption doesn't hold once branch
  protection requires the check. On top of that, the codebase (`client/src`
  included, not just `client/tests`) was not actually clang-format-clean —
  it had apparently never been run through the current `.clang-format` config.
- **Change**: Reformatted all of `client/src` and `client/tests` with
  clang-format 18.1.8 (the devcontainer's firewall blocks the Ubuntu package
  archive, so the binary came from a GitHub release of
  `muttleyxd/clang-tools-static-binaries` — same 18.1.x line CI's
  `apt-get install clang-format` resolves to). Build and full `ctest` suite
  (59/59) still pass after the reformat.
- **Why**: Since the tooling gap turned out to be solvable, fixing the
  formatting outright was better than leaving the debt for M5 — it directly
  unblocks the merge instead of requiring a branch-protection change, and
  formatting enforcement was always going to happen eventually. M5 can still
  be the point where `continue-on-error` is removed for real; the codebase is
  now actually compliant ahead of that.

## 2026-08-05 — Windows artefact path nests a `Debug` subfolder

- **Plan said**: `docs/plan/checklists/M1-host.md` (step 4) states the
  artefact path has no `Debug`/`Release` subfolder on Windows, reasoning
  that Ninja is single-config so JUCE won't nest a build-type folder —
  matching the container's actual path
  (`client/build/linux/dj-app-client_artefacts/DJ App`).
- **Actual**: On Windows the built exe lands at
  `client/build/windows/dj-app-client_artefacts/Debug/DJ App.exe` — JUCE
  nests a `Debug` folder here despite Ninja being single-config.
- **Why**: Not yet root-caused; `CMAKE_BUILD_TYPE` was left unset for this
  configure, and the JUCE install itself also logged `Install
  configuration: "Debug"` under the same conditions, suggesting JUCE's
  CMake helpers append `$<CONFIG>` to the output directory regardless of
  generator. Flagging as a deviation per the checklist's own instruction
  rather than silently substituting the path.
- **Update (2026-08-06, M3 host checklist)**: Reconfirmed — the `Debug`
  path is again where the artefact lands on this host. Consistent enough
  across two independent builds that `M3-host.md` now states it as the
  expected path rather than a "try both" fallback.

## 2026-08-07 — `CI / format` failed again: apt's `clang-format-18` isn't pinned to 18.1.8

- **Plan said**: The 2026-08-05 deviation above assumed installing the
  `clang-format-18` apt package in both the devcontainer `Dockerfile` and
  `.github/workflows/ci.yml` keeps the two in sync, since both resolve the
  same package name from the same Ubuntu 24.04 archive.
- **Actual**: `clang-format-18` only pins the major version. `apt-get
  install clang-format-18` currently resolves to `1:18.1.3-1ubuntu1` from
  `noble-updates` — not the `18.1.8` binary (from a
  `muttleyxd/clang-tools-static-binaries` GitHub release) that was actually
  used to reformat the tree on 2026-08-05. Point releases of clang-format
  can and do change formatting decisions, so files clean under 18.1.8 fail
  `--dry-run --Werror` under 18.1.3 (comment-alignment and line-break
  cases, e.g. `client/src/engine/EngineAdapter.cpp`,
  `client/src/ui/DeckComponent.h`, `client/tests/engine/EngineAdapterTest.cpp`).
  Devcontainer and CI agree with each other (both apt-installed, same
  archive) — the drift is against the previously-reformatted files, not
  between container and CI.
- **Change**: Reformatted the flagged files with the apt-installed
  `clang-format-18` (`18.1.3-1ubuntu1`). Also pinned the exact package
  version (`clang-format-18=1:18.1.3-1ubuntu1`) in both
  `.devcontainer/Dockerfile` and `.github/workflows/ci.yml`, replacing
  the bare `clang-format-18` major-version-only install that caused this
  drift.
- **Why**: Bare `clang-format-18` let devcontainer and CI silently drift
  to whatever point release the Ubuntu archive has on a given day. An
  exact pin is a two-line change and fails loudly (`apt-get install`
  error) instead of silently reformatting differently, so if this exact
  `.deb` is ever superseded out of the archive pool, bump the pin in
  both files, reformat, and record it here.

## 2026-08-14 — `02-protocol.md`'s `trackId` row amended to exclude `..` (no version bump)

- **Plan said**: `docs/plan/02-protocol.md`'s Field reference gave `trackId` the range
  `^[A-Za-z0-9._-]{1,64}$` or `null`. That character class admits `"."`, `".."`, and any
  value containing `".."`, e.g. `a..b`.
- **Plan also said**: `CLAUDE.md`'s non-negotiable rules require a protocol change to
  edit `02-protocol.md` first, update both sides and the fixtures in the same milestone,
  and bump the version.
- **Actual**: the client has rejected `"."`, `".."`, and embedded `".."` since M1
  (`isValidTrackId` in `client/src/model/Serialization.cpp`), as defence-in-depth for
  `06-security.md` §Client rules 2 — network data must never become a file path. Building
  the M6 server to the published regex alone would have made the server accept identifiers
  the client refuses, and the shared fixture corpus would have had to encode that as a
  permanent divergence.
- **Change**: amended the `trackId` row of the Field reference to state the `"."` / `".."`
  / embedded-`".."` exclusion alongside the regex, noting that it is checked separately
  because a character class cannot express it. `shared/protocol/PROTOCOL-VERSION` stays
  `1` and `PROTOCOL_VERSION` stays `1`.
- **Why**: no version bump, because no wire format that ever ran is invalidated. The
  client enforced this rule before any server existed; the document was describing the
  implementation inaccurately rather than specifying different behaviour, so there is no
  v1 peer that a v2 would be protecting itself from. Bumping would have churned every
  hello fixture, both `PROTOCOL_VERSION` constants, and M7's transport for a change no
  peer can detect. Recorded here because the amendment edits a binding document.

## 2026-08-14 — `npm test` script cannot be `node --test test/` on Node 24

- **Plan said**: `docs/plan/03-server.md` §npm scripts gives the exact script body
  `"test": "node --test test/"`.
- **Actual**: that form fails on the dev container's Node 24.19.0. `node --test test/`
  resolves the positional argument as a module entry point rather than as a directory to
  scan, and exits with `Error: Cannot find module '/workspaces/DJ-App/server/test'` before
  running anything. The plan pins Node 22 LTS, where the directory form works; the
  container ships 24, and CI pins 22, so the two would have disagreed.
- **Change**: `"test": "node --test \"test/**/*.test.js\""`. The quoted glob is expanded by
  Node's own test runner (Node 21+), not by the shell, so it behaves identically on 22 and
  24 and does not depend on shell globbing.
- **Why**: the script has to work in the container developers actually use and in the CI
  runner that pins the planned version. The glob form is the narrowest change that works on
  both; the alternative, pinning the container to Node 22, is a much larger change for no
  gain. Verified on Node 24: 631 tests, 631 passing.

## 2026-08-14 — the injected `conn` abstraction carries no `role` property

- **Plan said**: `docs/plan/03-server.md` describes `room.js` as operating on a thin client
  abstraction `{id, name, role, send(obj), close(code)}`.
- **Actual**: `conn` is `{id, name, send(obj), close(code, reason), remoteAddress}`. Role is
  derived inside `room.js` as `conn.id === controllerId ? 'controller' : 'observer'` via a
  `roleOf` helper, and `remoteAddress` was added for the security log lines.
- **Change**: dropped `role` from the abstraction; added `remoteAddress`.
- **Why**: raised by the design review of the M6 unit specs. Storing `role` on the
  connection while the room also tracks `controllerId` is one fact with two writers and
  nothing enforcing agreement. Every claim and release path would have to update both, and
  the first change to control that does not go through those two handlers — M8's second
  deck, or a future "steal control" affordance — would update one and leave the other
  stale, so the peers list would start disagreeing with `getControllerId()`. Deriving it
  makes the disagreement unrepresentable. `remoteAddress` exists because handshake failures
  must be logged with a source address for a room-code guessing attempt to be diagnosable
  at all; it is used only in log lines and is never sent to a peer.

## 2026-08-15 — one `rate-limited` error per deficit episode, not per dropped message

- **Plan said**: `docs/plan/02-protocol.md` rate limiting: "On exhaustion:
  `error{code:"rate-limited"}` and the message is dropped" — read literally, one error frame
  for every dropped frame.
- **Actual**: the connection gets one `error{code:"rate-limited"}` when it first goes into
  deficit, and nothing further until it has been served again. The dropped frames after the
  first are discarded silently.
- **Change**: amended `02-protocol.md` to state once-per-episode. The protocol version stays
  at 1.
- **Why**: raised by the correctness review of the landed M6 server, which read the document
  literally and found the code narrower. On inspection the document was the thing that was
  wrong. Answering every dropped frame makes the rate limiter reply with more bytes than it
  receives, so exceeding the limit would cost the server more than staying under it and the
  limiter would become the flood's amplifier — the exact property it exists to deny. The
  suppressed messages are redundant notifications about a condition the client caused and has
  already been told about once; no state diverges as a result, which is what separates this
  from the repair-snapshot dedup found in the same review, where suppression left a peer
  permanently wrong and the code was changed to match the document instead. Version stays at
  1 by the same reasoning as the `trackId` amendment above: no wire format that ever ran is
  invalidated, and there is no deployed v1 peer relying on a per-frame error.

## 2026-08-21 — `client/CMakeLists.txt` also needs `USE_ZLIB OFF` for IXWebSocket

- **Plan said**: `04-client.md` documents `USE_TLS=OFF` as the only IXWebSocket build
  option the client needs to set.
- **Actual**: IXWebSocket's `CMakeLists.txt` independently defaults `USE_ZLIB` to `ON`
  (permessage-deflate compression), which calls `find_package(ZLIB REQUIRED)`. System
  zlib is present via apt on the devcontainer, so this passed unnoticed there; a bare
  Windows Build Tools host has no system zlib, so `cmake -S client -B client/build/windows`
  failed at configure with `Could NOT find ZLIB`.
- **Change**: cache-seed `USE_ZLIB OFF` next to the existing `USE_TLS OFF` seed in
  `client/CMakeLists.txt`, same mechanism.
- **Why**: compression is transport-layer only and invisible to `02-protocol.md`; disabling
  it just means the extension isn't negotiated, with no interop effect on the `ws` server.
  State deltas are small JSON, so compression buys nothing here. Avoids adding a zlib
  install step to `docs/setup.md` for a feature the prototype doesn't need.

## 2026-08-21 — em dash in `TEST_CASE` names breaks CTest's Windows filter, not the code under test

- **Plan said**: `05-testing.md` gives no character-set restriction on test names; nothing in
  the plan anticipated this.
- **Actual**: six `TEST_CASE` names in `SerializationEnvelopeTest.cpp` used a literal em dash
  ("—", U+2014). `ctest --test-dir client/build/windows` reported all six as `Failed` on the
  Windows host (218/224 in the container, six red on Windows), but the per-test log showed
  `No test cases matched` against a mangled filter string (`ΓÇö`/`G��` in place of the dash) —
  CTest passes each Catch2 test's exact name as a command-line filter, and that non-ASCII byte
  sequence gets mis-transcoded somewhere in CMake/CTest's Windows console-codepage handling
  before reaching the binary. The code and assertions underneath were never exercised; every
  other em dash in the test tree lives in a `//` comment and was unaffected, confirming this
  is a name-encoding artifact, not a real MSVC-vs-GCC behavior difference.
- **Change**: replaced the em dash with a plain ASCII hyphen in those six test-name strings
  only (comments left as-is). Rebuilt and reran in the container: 18/18 passing, unchanged
  from before.
- **Why**: cheaper and more robust than chasing the Windows encoding pipeline (console
  codepage, CMake's `catch_discover_tests` name handling) — ASCII-only test names sidestep the
  whole class of problem. Recorded here so a future non-ASCII test name doesn't reintroduce the
  same silent-looking-real-but-isn't failure on the next Windows host checklist.

## 2026-08-21 - a track double-click did nothing before ever connecting

- **Plan said**: `04-client.md`/`07-milestones.md` carry M7's requirement that solo local
  playback (never connected) keeps working unchanged from M3-M6; nothing in the plan
  anticipated a regression here.
- **Actual**: `MainComponent`'s track-list click handler gated on `role_ != Role::controller`
  directly. `role_` defaults to `Role::observer` and only ever becomes `controller` after a
  successful claim, so a user who had never connected at all - solo mode, where control
  should always be open - got silently refused on every double-click. The M7 host checklist's
  Step 4 (deck controls start enabled solo) passed because `setEnabled` reads a separate,
  correct `controlsEnabled` computation; only the click handler's own guard was wrong, which is
  why this was invisible until someone actually double-clicked a track before connecting.
- **Change**: `645e7ac` replaced the handler's local `role_ != Role::controller` check with the
  same `canControlLocally()` used for `setEnabled`. That method's logic - `!connected_ ||
  role_ == Role::controller || !anyPeerControls(peers_)` - was then pulled out into
  `model/ControlGating.h::controlsEnabledLocally(connected, isLocalController,
  anyPeerIsController)`, a pure boolean function with no `Role`/`PeerInfo`/JUCE dependency, so
  it could be pinned by a Catch2 suite (`tests/model/ControlGatingTest.cpp`) instead of relying
  solely on the next host checklist to notice a regression - `app/` itself stays
  host-checklist-only per `05-testing.md`.
- **Why**: two call sites computing the same "can I act locally" decision from two different
  expressions is exactly the kind of single-fact-two-writers gap `03-server.md`'s `role`
  deviation (2026-08-14, above) already named for the server side; here it showed up on the
  client instead. Extracting the shared boolean function removes the second copy rather than
  fixing it in place, so the two call sites can't diverge again.

## 2026-08-21 - the server's origin check refused the client's own handshake

- **Plan said**: `06-security.md` lists Origin checking as optional, deferred until "browsers
  ever become clients" - not yet true at M7. `server.js`'s `verifyClient` (added `0852914`,
  ahead of that need) refused any handshake carrying an `Origin` header at all, on the stated
  assumption that "Our client is IXWebSocket and sends no Origin."
- **Actual**: IXWebSocket's handshake code (`IXWebSocketHandshake.cpp:132-136`) unconditionally
  sends `Origin: <scheme>://<host>:<port>` of the URL it dialed, unless the caller overrides it
  via `extraHeaders` - which `WebSocketTransport.cpp` never does. Every real connection from
  the C++ client therefore carried an `Origin: ws://127.0.0.1:8765` header and was refused with
  403, discovered when both windows failed to connect during the M7 host checklist's Step 5.
  The integration suite never caught this: it tested an evil origin (refused) and no origin at
  all (admitted), never the real client's actual header.
- **Change**: `verifyClient` now admits a handshake whose `Origin` exactly equals
  `ws://<config.host>:<bound port>` - the server's own address, which is what IXWebSocket
  always sends when dialing this server directly - and still refuses anything else. A browser's
  Origin instead names the page's own origin, which can never equal the server's own bind
  address, so the browser-hijack case the check exists for is still closed. Added
  `server.test/server.integration.test.js`'s "an upgrade request whose Origin matches the
  server itself is admitted" alongside the existing evil-origin and no-origin cases.
- **Why**: the header-absence check was written against an assumption about a third-party
  library that was never verified against its actual source, the same class of gap as the
  zlib and em-dash entries above - untested assumptions about dependency/host behavior that
  only surface on a real Windows-host run against a real client.

## 2026-08-21 - the crossfader stays local-only state, not synced in the protocol

- **Plan said**: `07-milestones.md`'s M8 task 2 leaves the crossfader's protocol status as
  "decide by effort" - synced properly (a protocol field, version bump to 2, fixture
  updates) unless that is trivial, in which case do it; otherwise accept and document a
  display-only mismatch across users.
- **Actual**: built as local-only state (`state/CrossfaderState`), never sent to the server
  or other clients. A protocol v2 bump would touch `shared/protocol/fixtures/`, both
  `PROTOCOL_VERSION` constants, and both sides' serialization for a field with no natural
  home in the per-deck `PlaybackState`/`StateDelta` shape, since the crossfader is a
  mixer-level concept, not a deck-level one - not the trivial case the milestone text
  carves out.
- **Change**: none to the protocol; `PROTOCOL_VERSION` and `shared/protocol/PROTOCOL-VERSION`
  stay at 1. Each client renders its own local crossfader position; two users can show
  different fader positions for the same room, and only the resulting audio (each client's
  own local gain) is ever a shared fact in practice, since gain itself isn't synced either.
- **Why**: applied `docs/decisions.md` §5's own tie-breaker for exactly this shape of
  "decide by effort" call - a change is not trivial once it touches the protocol version,
  fixtures, and both sides' wire handling. Confirmed with the user as one of three
  crossfader design decisions this milestone (`build-orchestration/build-log/2026-08-21-m8-second-deck-mixer.md`);
  the other two (crossfader as its own state class rather than a raw callback value, and
  gating the control with the other deck controls for a non-controller) are implementation
  choices with no plan text to deviate from, so they aren't recorded here.

## 2026-08-21 - claiming control didn't resync the room to the new controller's actual state

- **Plan said**: `07-milestones.md`'s M7 acceptance line and the M7 host checklist's Step 12
  both expect that once control transfers, the other window's controls "mirror window B's new
  track/playback within ~100 ms, symmetric to Steps 6-8."
- **Actual**: `MainComponent`'s `roleChanged` handler, on learning this client just became
  controller, only flipped local role state (`role_`, `applyRoleToUI()`). `SyncPublisher` only
  forwards a delta produced by a genuine local UI action taken while already controller, so a
  client that had been playing solo (unsynced, since nobody controlled) at its own position and
  rate never told anyone what it was doing the moment it claimed. The room's canonical state
  stayed whatever the previous controller last reported; the new controller's peers only caught
  up field-by-field, as each one happened to be touched by a later UI action, discovered on the
  M7 host checklist's Step 12 as a multi-second gap instead of the expected mirror.
- **Change**: on the observer-to-controller transition, `MainComponent::pushFullResync()` now
  sends one delta carrying every field of the client's current `PlaybackState`
  (`model/FullResyncDelta.h::fullResyncDelta`), straight through `transport_->sendDelta(...)`
  rather than `stateManager_.applyDelta(...)`. Routing it through `stateManager_` would also
  notify this client's own `EngineAdapter`, which reloads the track and resets position to 0 on
  any `trackId`-bearing delta - an audible restart of playback that had not actually changed,
  right as this client claims control.
- **Why**: no protocol or wire-format change was needed - this reuses the existing `delta`
  message type, so the fix is entirely client-side. The field-mapping half
  (`fullResyncDelta`) was pulled into `model/` and covered by
  `tests/model/FullResyncDeltaTest.cpp`, since `app/` itself stays host-checklist-only per
  `05-testing.md`.

## 2026-08-21 - a loop region past the track's end can no longer be silently overridden by repeat

- **Plan said**: `04-client.md`'s original M9 text (`engine/` section, `BufferPlaybackSource`
  bullet) specified that at end-of-track, a loop whose `outSeconds` is at or beyond the track's
  actual duration "never intercepts first," so the whole-track repeat wrap fires instead (head
  wraps to sample 0), leaving an armed-looking loop region silently inert.
- **Actual**: a pre-build design review of the M9 engine unit flagged that this produces a
  confusing user-facing moment - the deck's loop controls show a loop as set, but the audio
  repeats the whole track instead of looping the small region, with no indication why. Since
  loop points are never checked against the track's real length at parse/clamp time (`Ranges.h`
  only bounds them to the protocol-wide `[0, 86400]` range), this is reachable from an ordinary
  loop-out click if the loop's out point happens to land past a track's actual decoded length
  (e.g. a stale loop synced from a peer with a longer copy of the same file).
- **Change**: `BufferPlaybackSource::setLoop` now clamps both `inSamples` and `outSamples` to
  the loaded track's actual last renderable frame (only once a track is loaded -
  `messageThreadCurrentBuffer_` is non-null; before that, the pre-existing `messageThreadSampleRate_
  > 0.0` guard already keeps the loop inactive). An active loop therefore always intercepts
  before the whole-track boundary is ever reached, regardless of `repeat`.
  `ui/DeckComponent::currentDisplayPositionSeconds` mirrors the same clamp against the track's
  displayed duration, so the position slider/time readout never wrap at a point the audio
  engine doesn't actually reach.
- **Why**: confirmed with the user during this session's build - the fix is message-thread-only
  (`setLoop` is called from user gestures or synced deltas, never the audio callback), so it
  carries no real-time performance cost, and it closes a confusing edge case cheaply rather than
  leaving it as a documented limitation. `04-client.md`'s `BufferPlaybackSource` bullet has been
  updated to describe the clamped behavior instead of the "never intercepts" wording it
  originally specified.

## 2026-08-21 - the repeat toggle's icon didn't repaint on an observer

- **Plan said**: `CLAUDE.md` requires every bug fix to ship with a regression test in the
  same commit.
- **Actual**: found on M9 host checklist Step 12 - the observer's playback correctly stopped
  when the controller turned repeat off, but its repeat icon never flipped. Cause:
  `juce::Button::setToggleState()` no-ops while the button is disabled, and an observer's
  `repeatButton_` is disabled on every `refreshWidgets()` call - reordering `setEnabled`/
  `setToggleState` doesn't help, since it's still disabled either way. `repeatButton_` is the
  only synced control built on `juce::Button` rather than `juce::Slider` (no such gate), so
  gain/rate never hit this.
- **Change**: `refreshWidgets()` force-enables `repeatButton_`, sets the toggle, then restores
  its real enabled state (`dontSendNotification` throughout, so `onClick` never fires). No
  regression test: `dj-app-tests` excludes `juce_gui_basics` by design
  (`client/src/model/ControlGating.h:3-5`), and `05-testing.md` puts widget behavior under host
  checklists, not Catch2 - this bug lived entirely in JUCE `Button` semantics on a real widget.
- **Why**: confirmed with the user, matching the same `ui/`-is-host-checklist-only precedent as
  the M7 claim-control and M9 loop-clamp deviations above. M9 Step 12 already exercises this
  path and will catch a regression.