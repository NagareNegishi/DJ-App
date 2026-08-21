# M8 — second deck + mixer (build session, review deferred)

Two decks, a crossfader, deck B synced over the wire. This session covers the build only:
deck B wiring, the server's deck B integration test, the crossfader/mixer unit, and a
whitebox pass. The milestone is not done - the review layer (advisers), the `M8-host.md`
checklist, and the user's confirmation pass are explicitly deferred to a following session.
Container green: 265 client tests (up from 239), 673 server tests (up from 669), clean
build. No `PROGRESS.md` line yet - that waits for the whole milestone, same split M6 used
(`build-log/2026-08-14-m6-server-protocol.md` then `2026-08-15-m6-review.md`).

## The three units

**Deck B wiring** (`app/MainComponent`): a second `JuceAudioEngine`, `EngineAdapter`,
`PositionClock`, and `DeckComponent`, each constructed the same way deck A's already was.
This confirmed the deck-keying rule from `01-architecture.md` actually held since M1: no
interface on `EngineAdapter`, `PositionClock`, `DeckComponent`, or `StateManager` needed to
change, it was pure composition-root wiring. Track loading into deck B goes through a new
"Load -> B" button rather than repurposing the existing track-list double-click (which stays
deck-A-only, unchanged). `computeResumePositionSeconds` became deck-parameterized, and
`pushFullResync` now sends both decks' state on claiming control, not just A's.

**Server deck B integration test**: the server already fully supported deck B (lazy
creation, independent per-deck state) with fake-client unit coverage from M6, but had never
been exercised over a real socket. Added four real-socket cases to
`server.integration.test.js`: fresh-room snapshot has no B key, a deck-B delta broadcasts
correctly with deck independence preserved, and an invalid deck id draws `bad-message`
without closing the connection. No server source changes, test-only.

**Mixer/crossfader**: three design decisions were made with the user across an extended
back-and-forth (not just handed the cheap default), after a design-adviser review of the
initial proposal flagged real risk in a naive version. See Decisions below.

## Decisions

The milestone's own plan text left the crossfader's protocol status as "decide by effort" -
closed as **local-only, not synced**, per `docs/decisions.md` §5's own tie-breaker: a
protocol v2 bump touches `shared/protocol/fixtures/`, both `PROTOCOL_VERSION` constants, and
both sides' serialization, and the crossfader is a mixer-level concept with no natural home
in the per-deck `PlaybackState`/`StateDelta` shape regardless. Not yet recorded as a
`DEVIATIONS.md` entry - that's part of what's left for the review session, since the
milestone text explicitly requires the call to be documented there.

Three narrower decisions, made with the user one at a time rather than bundled, after a
design-adviser pass (`prompt-log/S8-adviser-1.md`) found real risk in the initial "direct
callback into `EngineAdapter`" proposal - specifically that it was the only UI-to-audio path
in the app not observable as state, had no initial-push (engine and fader would disagree
until first touch), duplicated the gain-composition expression in two places, and left the
milestone's own required `DEVIATIONS.md`/`decisions.md` documentation step out entirely:

1. **The crossfader's position is its own local-only state class** (`state/CrossfaderState`,
   value + listener tokens, same shape `StateManager` already uses), not a bare callback
   value and not a new field on synced state. `EngineAdapter` subscribes to it exactly the
   way it subscribes to `StateManager`, via a new optional `attachCrossfader(CrossfaderState&)`
   - deliberately not a constructor parameter, so `EngineAdapterTest.cpp`'s existing
   construction call sites needed zero changes. Rejected the cheaper "just pass a raw float
   into a setter" version once the user pushed on whether picking the easy path now would
   force a rebuild later - the answer was yes (a protocol-v2 crossfader would need the state
   object to exist regardless), so the state object was built now.
2. **Strict equal-power curve** (`model/CrossfaderCurve.h::equalPowerCrossfade` - real
   DJ-mixer behavior, ~-3dB dip at center, not normalized to unity), matching what the
   milestone text actually names. Documented in the header comment only, with no on-screen
   label or note: the user explicitly rejected adding UI-visible explanatory text ("don't
   make noise").
3. **Gates with the other deck controls for a non-controller** (`MixerComponent::
   setControlsEnabled`, wired into `applyRoleToUI` alongside the decks and track list), even
   though the control's actual effect is local-only and doesn't touch shared state - a
   consistency/simplicity call the user made explicitly after walking through and rejecting
   the technical argument for leaving it always-enabled. The technical argument was sound,
   and the user's product-feel preference for a uniformly-locked screen won anyway.)

`EngineAdapter` gained one private `pushEffectiveGain(float gain)` helper as the single place
`state.gain × crossfader multiplier` gets computed, called from both the existing
`handleDelta` gain branch and the crossfader's listener callback - closing the
two-independent-writers risk the design review flagged by name (the same "one fact, two
writers" class of bug as the server's `conn.role` deviation, `DEVIATIONS.md` 2026-08-14).
`attachCrossfader` pushes once immediately on attach, using the crossfader's actual current
position, so the engine and the on-screen fader agree before the user's first touch.

## Whitebox pass and an infrastructure mistake worth flagging

The whitebox pass (`prompt-log/S8-whitebox-1.md`) targeted `EngineAdapter`'s crossfader
composition and `CrossfaderState`'s notification-walk internals (the latter's file comment
explicitly invites re-entrancy scrutiny, referencing the same bug class `StateManager` had in
M4 - a listener unsubscribing itself mid-notification). It found two real bugs (below) and
also exposed a process mistake: the black-box test files for `CrossfaderCurve` and
`CrossfaderState` had been copied into the working tree and built/tested successfully, but
never `git commit`-ed, so the whitebox worktree (cut via `git worktree add` from the last
commit) silently didn't have them. Its own new `CrossfaderStateTest.cpp` was therefore
written from scratch rather than as an extension, colliding by path with the uncommitted
black-box file. Resolved by hand: committed the missed files first, then manually reconciled
the whitebox agent's new test cases into the already-existing files rather than attempting an
automated merge (a real add/add conflict, not something `agent-worktree.sh merge` can
resolve). Lesson for future sessions: **commit blackbox-tester output into the main checkout
immediately after copying it out of `.agent-scope/`**, before cutting any further worktrees -
don't let it sit as uncommitted working-tree state.

## Unresolved (closed 2026-08-21, see `2026-08-21-m8-review-fixes.md`)

Both bugs below are fixed as of the following session's build-log; the write-up
stays as-is below as the record of what the whitebox pass found and why, since
it remains accurate history. Treat "Unresolved" in this section's own heading
as stale - read the linked follow-up for current status.

## Unresolved - for the next session to fix directly, no re-diagnosis needed

Both bugs are pinned by passing tests that currently assert the *buggy* behavior as
documented fact (not xfail/skip) - fixing either requires flipping the corresponding
assertion(s) in the same commit as the fix, per this project's own regression-test rule.

**1. Medium - `EngineAdapter::attachCrossfader` called twice leaks the first listener
registration.**
- Where: `client/src/engine/EngineAdapter.cpp`, `attachCrossfader` (the
  `crossfaderListenerToken_ = crossfader_->addListener(...)` line) and `~EngineAdapter`
  (the `if (crossfader_ != nullptr) crossfader_->removeListener(crossfaderListenerToken_);`
  line).
- Mechanism: a second `attachCrossfader` call on the same `CrossfaderState` overwrites
  `crossfaderListenerToken_` without first removing the previous registration under that
  token. The destructor only ever removes the *last* token, so the first listener - a lambda
  capturing `this` - stays registered on the crossfader for as long as the crossfader
  outlives the adapter. Nothing in the current composition root ever calls `attachCrossfader`
  twice, so this is latent, not live. If it ever is (e.g. a future re-wiring path), the
  registered-but-orphaned lambda is a use-after-free once the adapter is destroyed.
- Proven by: `client/tests/engine/EngineAdapterTest.cpp`, the `TEST_CASE` tagged
  `[finding]` ("attachCrossfader called twice... pushes gain twice instead of once") -
  asserts `setGainCallsFromThisMove == 2`, i.e. it currently pins the bug as expected
  behavior.
- Fix direction: guard `attachCrossfader` to remove any existing registration
  (`if (crossfader_ != nullptr) crossfader_->removeListener(crossfaderListenerToken_);`)
  before installing the new one - the same pattern the destructor already uses, just also
  run at the top of `attachCrossfader`. Then flip the `[finding]` test's assertion to
  `== 1` and drop the `[finding]` tag once fixed.

**2. Low - `CrossfaderState::setPosition` double-notifies downstream listeners on
re-entrant `setPosition` calls.**
- Where: `client/src/state/CrossfaderState.cpp`, `setPosition`, specifically
  `listenerCopy(position_)` inside the notification walk - it reads the live `position_`
  member fresh on every step rather than a value snapshotted when that particular walk
  began.
- Mechanism: if a listener calls `setPosition` again from inside its own callback (a
  genuinely different value), the nested call runs its own full notification walk and
  returns. The *outer* walk then resumes from where it left off and re-notifies every
  listener registered after the re-entrant one a second time, because it re-reads the
  now-changed `position_` rather than the value it was originally walking for. State itself
  never corrupts (both firings observe the same, already-settled value), and it's currently
  harmless because `EngineAdapter::pushEffectiveGain` is idempotent for a repeated identical
  gain - but it would be user-visible the moment a future listener has a non-idempotent
  side effect (e.g. incrementing a counter, appending to a log).
- Proven by: `client/tests/state/CrossfaderStateTest.cpp`, "a listener calling setPosition
  again re-entrantly..." - asserts `firedCountFor1 == 2` and `firedCountFor2 == 2` for a
  single logical outer change.
- Fix direction: snapshot the target value (or the token list) at the start of the walk
  rather than re-reading `position_` live on each step - e.g. capture `const float
  targetPosition = clamped;` before the loop and notify with that, or (matching
  `StateManager::applyDelta`'s own discipline more closely) accept that a re-entrant
  `setPosition` mid-walk should simply not resume the outer walk at all once the value it
  was walking for has been superseded. Whichever approach is chosen, flip the two `== 2`
  assertions to `== 1` and update the test's docstring, which currently describes the bug as
  intentional ("Whitebox finding: listeners registered after the re-entrant one... each fire
  twice").

## Read first, next session

`docs/plan/07-milestones.md`'s M8 section for the acceptance bar and this file for what's
already done and the two fixes above. `prompt-log/S8-adviser-1.md` has the full
design-review reasoning behind the crossfader shape, worth a look if any of it needs
revisiting during the correctness/simplicity review pass.
