// Black-box tests for model/BeatSync.h's computeBeatSync(), derived purely
// from the spec handed to this agent (no implementation source was read).
//
// computeBeatSync(thisGrid, thisCurrentPositionSeconds, otherGrid,
//                  otherCurrentPositionSeconds, otherPlaybackRate = 1.0f,
//                  thisDurationSeconds = 0.0)
//   -> std::optional<BeatSyncResult{playbackRate, positionSeconds}>
//
// Per the spec:
// - nullopt iff either grid's bpm <= 0 (detection failed) - the only failure
//   signal, no in-band value doubles for it.
// - playbackRate = (otherGrid.bpm * otherPlaybackRate) / thisGrid.bpm,
//   clamped to [ranges::playbackRateMin, ranges::playbackRateMax].
// - Each deck's phase is wrapped using ITS OWN beat interval (60 / bpm), then
//   the other deck's phase is scaled into this deck's interval via the ratio
//   otherGrid.bpm / thisGrid.bpm (otherPlaybackRate cancels out of this step
//   completely - it's already folded into the tempo match above). The nudge
//   is the shortest signed correction, bounded to within half a beat
//   interval. A prior, buggy draft of this spec wrapped BOTH decks' phase
//   using thisGrid's interval, i.e. it only matched phases correctly when
//   the two BPMs happened to be equal - that bug must not reappear, see the
//   dedicated regression test below.
// - When thisDurationSeconds > 0 and the nudge would land outside
//   [0, thisDurationSeconds], positionSeconds falls back to
//   thisCurrentPositionSeconds UNCHANGED (not clamped to the boundary -
//   clamping would immediately re-trigger the engine's own end-of-track
//   self-stop). Either way, positionSeconds is then clamped to the
//   protocol-wide [ranges::positionSecondsMin, ranges::positionSecondsMax]
//   range.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/BeatSync.h"
#include "model/Ranges.h"

#include <cmath>
#include <optional>

using namespace djapp;

TEST_CASE("computeBeatSync returns nullopt when this deck's BPM detection failed", "[BeatSync]")
{
    BeatGrid thisGrid; // bpm defaults to 0 (detection failed)
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    auto result = computeBeatSync(thisGrid, 10.0, otherGrid, 10.0);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("computeBeatSync returns nullopt when the other deck's BPM detection failed", "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 120.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid; // bpm defaults to 0

    auto result = computeBeatSync(thisGrid, 10.0, otherGrid, 10.0);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("computeBeatSync returns nullopt when both decks' BPM detection failed", "[BeatSync]")
{
    BeatGrid thisGrid;
    BeatGrid otherGrid;

    auto result = computeBeatSync(thisGrid, 0.0, otherGrid, 0.0);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("computeBeatSync returns nullopt for a negative bpm, not just exactly zero", "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = -5.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    auto result = computeBeatSync(thisGrid, 10.0, otherGrid, 10.0);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("computeBeatSync: two decks already in phase at the same BPM yields a near-zero nudge "
          "and playbackRate ~1.0",
          "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 120.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    // Both positions sit exactly on a beat of their respective grids (20 and
    // 40 beat-intervals of 0.5s past their first beat), so the two decks are
    // already in phase.
    const double thisPos = 10.0;
    const double otherPos = 20.0;

    auto result = computeBeatSync(thisGrid, thisPos, otherGrid, otherPos);

    REQUIRE(result.has_value());
    CHECK(result->playbackRate == Catch::Approx(1.0f));
    CHECK(result->positionSeconds == Catch::Approx(thisPos).margin(1e-9));
}

TEST_CASE("computeBeatSync: phase nudge aligns this deck's phase to the OTHER deck's phase "
          "(regression guard against a self-quantize bug that ignores otherGrid/otherPosition)",
          "[BeatSync][regression]")
{
    // Same BPM on both decks, same firstBeatSeconds reference (0.0), so any
    // difference between a correct sync and a self-quantize is due purely to
    // whether otherCurrentPositionSeconds/otherGrid.firstBeatSeconds were
    // actually read.
    BeatGrid thisGrid;
    thisGrid.bpm = 120.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    // thisPos sits exactly on a beat of thisGrid (phase 0 relative to its own
    // grid) - a self-quantize implementation (which only reads thisGrid) would
    // therefore compute zero correction and leave the position unchanged at
    // 10.0, since "this" deck already looks perfectly aligned to *itself*.
    const double thisPos = 10.0;
    // otherPos is a quarter-of-a-beat-interval off the beat grid (phase 0.25
    // out of a 0.5s beat interval, i.e. 180 degrees / half a beat out of
    // phase with thisPos). A correct implementation must nudge "this" deck's
    // phase to match this, landing at 10.25, not leave it at 10.0.
    const double otherPos = 20.25;

    auto result = computeBeatSync(thisGrid, thisPos, otherGrid, otherPos);

    REQUIRE(result.has_value());
    // The correct, spec-faithful result: this deck's phase moved to match
    // the other deck's actual phase.
    CHECK(result->positionSeconds == Catch::Approx(10.25).margin(1e-9));
    // Explicit contrast: a self-quantize implementation (bug reintroduced)
    // would leave the position at 10.0 (already "in phase" with its own
    // grid) - assert that specific wrong value is NOT what we got.
    CHECK(result->positionSeconds != Catch::Approx(10.0).margin(1e-9));
}

TEST_CASE("computeBeatSync: a BPM ratio far above playbackRateMax is clamped to playbackRateMax", "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 1.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 100000.0; // ratio 100000x - guaranteed to exceed any sane playbackRateMax
    otherGrid.firstBeatSeconds = 0.0;

    auto result = computeBeatSync(thisGrid, 5.0, otherGrid, 5.0);

    REQUIRE(result.has_value());
    CHECK(result->playbackRate == ranges::playbackRateMax);
}

TEST_CASE("computeBeatSync: a BPM ratio far below playbackRateMin is clamped to playbackRateMin", "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 100000.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 1.0; // ratio 0.00001x - guaranteed to fall below any sane playbackRateMin
    otherGrid.firstBeatSeconds = 0.0;

    auto result = computeBeatSync(thisGrid, 5.0, otherGrid, 5.0);

    REQUIRE(result.has_value());
    CHECK(result->playbackRate == ranges::playbackRateMin);
}

TEST_CASE("computeBeatSync: the phase nudge never exceeds half a beat interval in magnitude", "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 100.0; // beat interval 0.6s
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 100.0;
    otherGrid.firstBeatSeconds = 0.0;

    const double beatIntervalSeconds = 60.0 / thisGrid.bpm;
    const double thisPos = 50.0;

    // Sweep the other deck's position across a full beat interval's worth of
    // relative offsets, including offsets that would require a large raw
    // (unwrapped) correction if the shortest-path wrap were not applied.
    const double offsets[] = {-0.59, -0.45, -0.3, -0.1, 0.0, 0.1, 0.3, 0.45, 0.59};

    for (double offset : offsets)
    {
        const double otherPos = thisPos + offset;
        auto result = computeBeatSync(thisGrid, thisPos, otherGrid, otherPos);
        REQUIRE(result.has_value());

        const double magnitude = std::abs(result->positionSeconds - thisPos);
        CHECK(magnitude <= beatIntervalSeconds / 2.0 + 1e-9);
    }
}

TEST_CASE("computeBeatSync: with thisDurationSeconds supplied, an overshoot past the end of a short "
          "track leaves the position unchanged instead of clamping to the track's duration",
          "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 120.0; // beat interval 0.5s
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    const double thisDurationSeconds = 5.0; // short track
    const double thisPos = 4.9;             // near the very end
    const double otherPos = 4.05;           // raw nudge pushes past 5.0 (target ~5.05) without clamping

    auto result = computeBeatSync(thisGrid, thisPos, otherGrid, otherPos, 1.0f, thisDurationSeconds);

    REQUIRE(result.has_value());
    // Falls back to the unchanged current position rather than clamping to the
    // boundary - clamping would land on the last renderable frame and
    // immediately re-trigger the engine's own end-of-track self-stop.
    CHECK(result->positionSeconds == Catch::Approx(thisPos).margin(1e-9));
    CHECK(result->positionSeconds < thisDurationSeconds);
}

TEST_CASE("computeBeatSync: without thisDurationSeconds supplied, the same raw nudge is left "
          "un-clamped by track length (only the protocol-wide range applies)",
          "[BeatSync]")
{
    BeatGrid thisGrid;
    thisGrid.bpm = 120.0;
    thisGrid.firstBeatSeconds = 0.0;
    BeatGrid otherGrid;
    otherGrid.bpm = 120.0;
    otherGrid.firstBeatSeconds = 0.0;

    const double thisPos = 4.9;
    const double otherPos = 4.05;

    // thisDurationSeconds defaults to 0.0, which disables the duration clamp.
    auto result = computeBeatSync(thisGrid, thisPos, otherGrid, otherPos);

    REQUIRE(result.has_value());
    // Same raw target as the previous test (~5.05), but this time nothing
    // clamps it down to a short track length - it is only bounded by the
    // protocol-wide position range, which 5.05 sits nowhere near.
    CHECK(result->positionSeconds == Catch::Approx(5.05).margin(1e-6));
    CHECK(result->positionSeconds >= ranges::positionSecondsMin);
    CHECK(result->positionSeconds <= ranges::positionSecondsMax);
}
