# M2 — AudioRepository

## What landed

`AudioRepository` (interface) and `LocalFileRepository` (implementation) in
`client/src/repository/`, plus a `TrackListComponent` in `client/src/ui/`, both wired
into `MainComponent` as the composition root. 59 tests in
`client/tests/repository/`, `ctest` green. A headless run confirmed the acceptance
behavior directly: a missing manifest, a bad id, a path-escaping file, a symlinked
file, a missing file, and (via test coverage) an oversized or corrupt decode each skip
with a distinct log line, and none of them crash the app.

## Decisions made during the spec review, before any code was written

- **Track duration.** Read once via a cheap `AudioFormatReader` header open (no PCM
  decode) at manifest-load time, kept separate from the full lazy decode-and-cache
  that `getAudioBuffer` still only does on demand. The alternative was to leave
  duration at 0 until a track happened to be loaded once, which would be visibly wrong
  on a first-paint track list.
- **Path safety.** Reject a manifest entry whose resolved file is a symlink, in
  addition to the plan's bare-filename and `isAChildOf` checks. `isAChildOf` is a
  textual comparison and doesn't see through a symlink pointing outside the root.
- **Decode robustness.** Bound the max decodable sample count before allocating, and
  wrap the decode in `try/catch`, so a corrupt or absurdly large audio file fails to
  `nullptr` instead of throwing past the interface boundary.
- **Assets root.** Resolve `client/assets/tracks` via a `CLIENT_SOURCE_DIR` compile
  definition (the source checkout path baked in at configure time), not
  executable-relative. This matches how the prototype is actually built and run in the
  container, with no extra copy step. Revisit if the app is ever packaged for
  distribution.

These were surfaced to the user as design and security review findings against the
spec, before implementation, and decided there. See
`build-orchestration/prompt-log/S1-unit-spec-m2-repository.md` for the full reasoning
behind each.

## What the post-implementation review caught, and what happened to it

- **TOCTOU on the symlink check** (security, low). The symlink and escape check ran
  once at manifest load but not again at decode time. Fixed by factoring both checks
  into a shared `isSafeToOpen()` helper called from both places.
- **Pure-logic sources listed by hand in two CMake targets** (design, medium). Every
  future milestone that adds testable non-GUI source (`engine/`, `state/`, `sync/`
  still to come) would otherwise need remembering in both the app target and the test
  target. Fixed with a `dj-app-core` library that compiles the shared sources once,
  linked by both. This had to become a `STATIC` library rather than the originally
  planned `OBJECT` library. JUCE's CMake integration attaches each module's
  implementation as an interface source on the module target itself, so an `OBJECT`
  library and the app target ended up each compiling their own copy of the same JUCE
  module translation unit, and the final link saw duplicate symbols. A `STATIC`
  archive is pulled in lazily (the linker only extracts a member to resolve an
  otherwise-unsatisfied symbol), which avoids the collision.
- **No message-thread assertion** (design, low). `AudioRepository`'s methods were
  documented as message-thread-only but nothing enforced it. Added
  `JUCE_ASSERT_MESSAGE_THREAD` to `LocalFileRepository`'s constructor and public
  methods, matching the convention `StateManager` will also follow from M4.
- **Wrong doc citation** (docs, low). `AudioRepository.h`'s threading comment cited
  `01-architecture.md`'s threading table for "synchronous load is fine," but that
  table actually describes an eventual async repository load. The real permission for
  a synchronous prototype load is in `04-client.md`. Reworded to cite the right source
  and frame the two documents as describing different points in time, not
  contradicting each other.
- **Rejected.** A correctness-adviser finding claimed a mono audio file with a
  corrupted header could pass the decode size guard and then overflow the `int` cast
  in `AudioBuffer::setSize`. The guard (`maxDecodedSampleCount` = 48000 x 3600 x 2 =
  345,600,000) is checked against `lengthInSamples x numChannels`. For any
  `numChannels >= 1` this already caps `lengthInSamples` at 345.6M, well under
  `INT_MAX` (2.147B), so the claimed overflow path isn't reachable. The adviser's own
  math was off by roughly 1000x. No fix applied.

## Process note

The black-box test files were moved into `client/tests/repository/` as loose files but
not committed before a later worktree was cut from `HEAD` for the white-box pass. That
worktree came up without them. Caught by the white-box tester's own report, which
flagged the discrepancy instead of silently working around it, then fixed by
committing the tests before continuing. Worth remembering for future sessions:
landing files into the working tree is not the same as landing them into history that
a new worktree will actually see.

## Unresolved risk

None outstanding. Every medium and low finding from the review layer was either fixed
or explicitly rejected with reasoning recorded above.
