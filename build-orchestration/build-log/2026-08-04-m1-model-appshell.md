# M1 — walking skeleton + domain model

Two independent units: the `model/` layer (types, strict JSON serialization,
range clamping) and the `app-shell` (JUCE window boilerplate). Sequenced
rather than run in parallel because both touch `CMakeLists.txt`.

## model/

Implements `PlaybackState`/`StateDelta`/`TrackMetadata`/`DeckId` (`Types.h`),
`toVar`/`fromVar<T>` (`Serialization.h/.cpp`), and `ranges::clamp`
(`Ranges.h`), per `docs/plan/04-client.md` and the Field reference in
`docs/plan/02-protocol.md`. This is the layer that will parse untrusted JSON
from a WebSocket server starting at M7, so it went through a security gate
before implementation and a second review pass after.

### Decisions made resolving spec ambiguity

- **Out-of-range numbers: clamp, not reject.** `docs/plan/04-client.md` and
  `02-protocol.md` disagreed on this: 04 said `fromVar` should reject
  out-of-range values, 02's Validation policy says the client clamps as
  defense-in-depth. Went with clamp. It's what 02 states explicitly, and
  it's the only reading under which `Ranges::clamp()` has a real job on
  incoming data. The one exception is `loop.inSeconds < outSeconds`, which
  can't be fixed by clamping two independent endpoints, so that's a
  parse-time rejection in `parseLoopValue`.
- **`StateDelta.deck` is required and strict** (rejects absent or non-`A`/`B`
  values) rather than silently defaulting to deck A. An unspecified or
  garbage deck field defaulting to A would let a malformed message quietly
  overwrite the one deck that exists before M8.
- **`trackId` additionally rejects `.`, `..`, and any embedded `..`** beyond
  what the `^[A-Za-z0-9._-]{1,64}$` regex alone excludes, since the charset
  permits those on their own.
- **`fromVar` is a function template with explicit specializations**
  (`fromVar<PlaybackState>`, `fromVar<StateDelta>`), not two overloads. The
  spec's two-functions-same-signature description isn't valid C++ overload
  resolution. `Result<T>` is `{bool ok; T value; juce::String error;}` with
  `operator bool()`/`operator*()`.

### Bug found and fixed in review

`Ranges::clamp(LoopPoints&)` clamps `inSeconds`/`outSeconds` independently.
A well-formed, correctly-ordered loop that's merely out of range (e.g.
`{90000, 100000}`) could clamp to `{86400, 86400}`, making
`inSeconds == outSeconds` and violating the same ordering invariant
`parseLoopValue` enforces at parse time. Fixed: if clamping a
`PlaybackState.loop` would leave it degenerate, the loop is cleared
(`nullopt`) instead of kept invalid. For `StateDelta`, the same collapse
clears the *inner* optional but leaves the *outer* one engaged, so it
becomes an explicit "clear the loop" delta rather than a field silently
dropped from the delta.

Also fixed: `gain`/`playbackRate`/`pitchOffsetSemitones` are validated as
finite `double`s then narrowed to `float`. A value finite as a double (e.g.
`1e300`) can narrow to `±Infinity`, silently defeating the "reject
non-finite" guarantee for those three fields specifically (`positionSeconds`
and `loop` are `double`-typed and unaffected). Fixed with a combined
validate-and-narrow helper that checks finiteness again after the cast.

### Review layer, deployed and skipped

- **security-adviser**: ran twice. Once on the spec before implementation
  (6 findings, all closed by tightening the spec), once on the landed code
  to confirm the tightened spec actually held (1 low finding: the float
  narrowing above).
- **correctness-adviser**: 1 medium finding (the loop-clamp bug above); the
  rest of the implementation checked out against the Field reference.
- **simplicity-adviser**: flagged real duplication between
  `fromVar<PlaybackState>` and `fromVar<StateDelta>` (~100 near-identical
  lines of per-field validation). Extracted shared parse helpers
  (`parseBool`, `parseFinite`, `parseFiniteFloat`, `parseTrackId`); each
  `fromVar` specialization keeps its own `hasProperty` guard and assignment
  (direct for `PlaybackState`, into a `std::optional` for `StateDelta`). No
  generic abstraction was needed there; plain assignment already works both
  ways.
- **design-adviser, performance-adviser, docs-adviser, legal-adviser,
  change-discipline-adviser**: not deployed. The spec was fully dictated by
  the plan (no new abstraction to design-review), nothing hot-path or
  unbounded, no new dependency, and the one implementer deviation (extending
  the loop ordering check to `PlaybackState.loop`, which the spec only
  required for `StateDelta.loop`) was disclosed and reasonable, not scope
  creep.

### Unresolved, logged as risk

90 `-Wfloat-equal` compiler warnings across the four test files (exact
equality assertions on floats after lossless round-trips: correct behavior,
noisy warning). Not flagged by any adviser, not fixed this session.

## app-shell

`Main.cpp` (JUCEApplication subclass) + `src/app/MainWindow` +
`src/app/MainComponent`, per `docs/plan/04-client.md`'s `app/` section.
Trivial GUI boilerplate. No adviser gate met its deploy-when trigger, no
blackbox suite (nothing spec-derived to test without a real display), no
whitebox pass (no internals worth reaching). Verified by a clean
compile and link in the container only; the actual window still needs
confirming on the Windows host.

## State at session end

`ctest` green, 38/38 (21 model black-box tests + 14 model white-box
regression tests replacing the collapsed-loop assertions + 3 new
float-overflow regression cases). Linux build green for both
`dj-app-client` and `dj-app-tests`. **M1's host checklist has not run yet.**
`docs/plan/checklists/M1-host.md` is written and ready, but the milestone
stays open in `PROGRESS.md` until the user confirms it on the Windows host.
