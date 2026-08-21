# M9 - track repeat (auto-replay)

Protocol bumped to v2 with one new field, `repeat` (boolean, default `true`), synced like
`playing`/`loop` rather than treated as local-only state (unlike M8's crossfader) since
whether a track stops or repeats is an actual audio difference every connected user hears.
Landed across four build units (server protocol/validation/fixtures, client model,
client engine, client UI) plus two review-driven fix rounds. Server 682/682, client 294/294,
both green.

## Decisions

**A loop past the track's end no longer loses to repeat.** The milestone's original text said
a loop whose `outSeconds` reaches past the track's actual duration should "never intercept,"
letting the whole-track repeat wrap take over instead - silently overriding a loop that looked
armed in the UI. Confirmed with the user before implementation: since the fix is
message-thread-only (no real-time cost) and closes a confusing edge case cheaply, both
`BufferPlaybackSource::setLoop` and `DeckComponent`'s display-side wrap now clamp a loop's
in/out bounds to the track's actual decoded length, so an armed loop always audibly intercepts
first. `04-client.md` and `DEVIATIONS.md` (2026-08-21 entry) were updated to describe this
instead of the original "never intercepts" wording - a post-implementation correctness review
caught that the code and the doc had drifted apart on exactly this point, and that the UI's
display wrap used unclamped bounds while the engine's used clamped ones, which would have made
the position readout count past the track's real duration whenever a loop's out-point exceeded
it. Both are now clamped consistently.

**Repeat's default-on behavior surviving an abandoned controller is not a bug.** Considered
during the pre-build security gate: a controller could start a repeating track and disconnect,
leaving it looping until someone claims control. Dismissed - any observer can already claim
control and pause in one click, identical to every other "control is unclaimed" situation the
protocol already has. No mitigation added.

## Bugs found by the review layers (both fixed, not deferred)

The build itself followed established patterns closely (mirroring the existing `playing`/`loop`
fields at every layer), but two review passes each found a real gap in propagating `repeat`
through paths that copy an entire `PlaybackState` rather than a single field at a time:

- **Whitebox pass**: `StateManager::applyDelta`'s merge block copied every other field
  (`trackId` through `loop`) into stored state but had no line for `repeat` - a repeat delta
  reached listeners and drove the engine correctly (both read the delta directly), but
  `StateManager`'s own stored value never updated, so anything reading `getState(deck).repeat`
  later (a snapshot builder, `DeckComponent`'s display anchor) saw a stale value. Fixed with the
  missing merge line; the whitebox-authored pinning test (`StateManagerTest.cpp`) had its
  `[!shouldfail]` tag removed once fixed.
- **Post-implementation correctness review**: the same class of gap existed in three more
  places that build a full-field `StateDelta` from a `PlaybackState` - `MainComponent::
  applyDeckSnapshot` (a joining/resyncing client's snapshot never carried `repeat`),
  `fullResyncDelta` (claiming control never pushed the claiming client's own `repeat` to the
  room), and `SyncPublisher`'s `mergeField` (a repeat toggle coalesced into an in-flight
  throttled delta got applied locally but never transmitted). All three were one-line
  omissions, same shape as the whitebox-found bug, fixed the same way.

This pattern - remembering to touch every *incremental* delta path but missing the *full-copy*
paths - is worth watching for the next time a `PlaybackState` field is added; there are now four
such full-copy sites (`applyDeckSnapshot`, `fullResyncDelta`, `SyncPublisher::mergeField`, and
`StateManager::applyDelta`'s own merge block), and a future field needs all four touched, not
just the obvious `Serialization.cpp`/`EngineAdapter` pair.

## Unresolved risk (logged, not fixed - both low severity)

- `Serialization.cpp`'s `parseBool` helper has a hard-coded `"playing must be a boolean"` error
  message reused for `repeat`'s validation, so a rejected non-boolean `repeat` is logged
  under the wrong field name. Cosmetic (rejection behavior itself is correct); fixing it means
  threading a field-name parameter through `parseBool`, deliberately deferred twice this session
  (once during Unit 2's build, once at review) to avoid scope creep on a boolean-only helper.
- `positionSeconds` has never been clamped to a track's actual duration on the client (only to
  the protocol-wide `[0, 86400]` range) - a pre-existing gap, not introduced by M9. Repeat's
  wrap logic makes it slightly more consequential: a seek past a track's real length used to hit
  the pre-M9 self-stop; now it lands at a position determined by modular arithmetic against the
  buffer length instead. Worth revisiting if a future milestone touches seek handling.

## Host checklist

`checklists/M9-host.md` written, not yet run - needs the user's Windows-host confirmation
before M9 closes, covering default-on repeat, the toggle, loop/repeat precedence, and (Steps
15-16 specifically) the two full-copy-path bugs above, since a passing basic loop/toggle test
alone would not have caught either.
