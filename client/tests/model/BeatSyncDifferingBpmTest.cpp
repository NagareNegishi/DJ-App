// model/ - BeatSyncDifferingBpmTest: black-box regression coverage for the
// computeBeatSync phase-alignment fix (see docs/plan spec
// "fix-sync-math-spec.md"). The pre-fix bug wrapped BOTH decks' phase using
// thisGrid's own beat interval, which only happens to be correct when the
// two decks' BPMs are equal - the existing BeatSyncTest.cpp suite is entirely
// equal-BPM cases and never caught it. This file closes that gap with
// differing-BPM pairs, non-1.0 otherPlaybackRate cases, and the corrected
// end-of-track fallback behaviour.
//
// NOTE ON HOW THESE EXPECTED VALUES WERE DERIVED: every expected
// positionSeconds below is a concrete number worked out by hand from the
// spec's formula (fmod-based phase wrap, ratio-scale, shortest-signed-delta
// correction), not by re-running the formula's steps inline in this file -
// doing the latter would just re-derive whatever bug the implementation has
// and always agree with it, defeating the point of an independent check.
// The one exception is the final `std::clamp(..., ranges::xMin, ranges::xMax)`
// wrapper, which is unavoidable since this file cannot read the concrete
// bound values in Ranges.h in this session (sandboxed to read only spec
// sources); wrapping the hand-derived raw value in the real clamp call keeps
// the assertion correct regardless of what those bounds actually are.
//
// SESSION CAVEAT: this session's sandbox blocked reading the existing
// client/tests/model/BeatSyncTest.cpp, client/src/model/BeatSync.h, and
// client/src/model/Ranges.h (outside the tester's read-only spec scope), so
// this file could not confirm the existing suite's exact helper names,
// namespace-using conventions, or Catch2 header set - see this task's
// Findings for what to double check when merging. Field names used below
// (BeatGrid::bpm, BeatGrid::firstBeatSeconds, BeatSyncResult::playbackRate,
// BeatSyncResult::positionSeconds) come directly from the spec's own
// pseudocode, not a guess. The local `makeGrid` helper lives in an anonymous
// namespace so it cannot collide at link time with any same-named helper the
// existing suite may already define.

#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/BeatSync.h"
#include "model/Ranges.h"

using namespace djapp;

namespace
{

BeatGrid makeGrid(double bpm, double firstBeatSeconds)
{
    BeatGrid grid;
    grid.bpm = bpm;
    grid.firstBeatSeconds = firstBeatSeconds;
    return grid;
}

} // namespace

TEST_CASE("computeBeatSync aligns phase using each deck's own beat interval when BPMs differ",
          "[BeatSync][differing-bpm]")
{
    SECTION("120 BPM this deck, 130 BPM other deck, both on the beat grid at firstBeatSeconds = 0")
    {
        // thisBeatInterval = 60/120 = 0.5s, otherBeatInterval = 60/130 = 6/13 s.
        // thisPos = 10.0 -> thisPhase = fmod(10.0, 0.5) = 0.0.
        // otherPos = 10.0 -> otherPhase = 10.0 - 21*(6/13) = 10 - 126/13 = 4/13 s.
        // targetThisPhase = fmod((130/120) * (4/13), 0.5) = fmod(1/3, 0.5) = 1/3.
        // delta = 1/3 - 0 = 1/3, which exceeds half the interval (0.25), so
        // delta -= 0.5 -> delta = 1/3 - 1/2 = -1/6.
        // targetPosition = 10.0 - 1/6 = 59/6.
        const BeatGrid thisGrid = makeGrid(120.0, 0.0);
        const BeatGrid otherGrid = makeGrid(130.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 10.0, otherGrid, 10.0, 1.0f);

        REQUIRE(result.has_value());

        const float expectedRate = std::clamp(130.0f / 120.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

        const double expectedPosition =
            std::clamp(59.0 / 6.0, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
    }

    SECTION("90 BPM this deck, 135 BPM other deck, nonzero firstBeatSeconds on both grids")
    {
        // thisBeatInterval = 60/90 = 2/3 s, otherBeatInterval = 60/135 = 4/9 s.
        // thisPhase = fmod(5.0 - 0.1, 2/3) = fmod(4.9, 2/3) = 49/10 - 14/3 = 7/30 s.
        // otherPhase = fmod(5.0 - 0.05, 4/9) = fmod(4.95, 4/9) = 99/20 - 44/9 = 11/180 s.
        // targetThisPhase = fmod((135/90) * (11/180), 2/3) = fmod(11/120, 2/3) = 11/120
        //   (11/120 is already less than the 2/3 s interval).
        // delta = 11/120 - 7/30 = 11/120 - 28/120 = -17/120, within +-1/3 so no wrap.
        // targetPosition = 5.0 - 17/120 = 583/120.
        const BeatGrid thisGrid = makeGrid(90.0, 0.1);
        const BeatGrid otherGrid = makeGrid(135.0, 0.05);

        const auto result = computeBeatSync(thisGrid, 5.0, otherGrid, 5.0, 1.0f);

        REQUIRE(result.has_value());

        const float expectedRate = std::clamp(135.0f / 90.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

        const double expectedPosition =
            std::clamp(583.0 / 120.0, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
    }

    SECTION("100 BPM this deck, 128 BPM other deck, delta needs the negative-side wrap correction")
    {
        // thisBeatInterval = 60/100 = 0.6s, otherBeatInterval = 60/128 = 0.46875s (exact).
        // thisPhase = fmod(3.5, 0.6) = 3.5 - 5*0.6 = 0.5.
        // otherPhase = fmod(3.28125, 0.46875) = 0 exactly (3.28125 = 7 * 0.46875).
        // targetThisPhase = fmod((128/100) * 0, 0.6) = 0.
        // delta = 0 - 0.5 = -0.5, which is below -half the interval (-0.3), so
        // delta += 0.6 -> delta = 0.1.
        // targetPosition = 3.5 + 0.1 = 3.6.
        // This section exercises the opposite wrap branch from the 120/130
        // section above (there delta was reduced by a full interval; here it's
        // increased by one), so both signed-wrap paths of the shortest-delta
        // correction get covered against a differing-BPM pair.
        const BeatGrid thisGrid = makeGrid(100.0, 0.0);
        const BeatGrid otherGrid = makeGrid(128.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 3.5, otherGrid, 3.28125, 1.0f);

        REQUIRE(result.has_value());

        const float expectedRate = std::clamp(128.0f / 100.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

        const double expectedPosition = std::clamp(3.6, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
    }
}

TEST_CASE("computeBeatSync folds the other deck's own playbackRate into the tempo match without "
          "double-counting it in the phase step",
          "[BeatSync][other-playback-rate]")
{
    SECTION("other deck pitched up 5% (otherPlaybackRate = 1.05)")
    {
        // effectiveOtherBpm = 100 * 1.05 = 105, so playbackRate = 105/120 = 0.875
        // (this is the value that must change with otherPlaybackRate).
        //
        // Phase step must use the RAW otherGrid.bpm/thisGrid.bpm ratio
        // (100/120 = 5/6), not multiplied by 1.05 again - the spec is explicit
        // that otherPlaybackRate cancels out of this step algebraically. If an
        // implementation double-counted it, targetThisPhase would come out as
        // 0.875 * 0.2 = 0.175 instead of the correct 5/6 * 0.2 = 1/6, and
        // targetPosition would be 2.175 instead of 13/6 - this section's
        // expected value pins the correct (non-double-counted) figure.
        //
        // thisBeatInterval = 0.5s, otherBeatInterval = 0.6s.
        // thisPhase = fmod(2.0, 0.5) = 0.0.
        // otherPhase = fmod(2.0, 0.6) = 2.0 - 3*0.6 = 0.2.
        // targetThisPhase = fmod((100/120) * 0.2, 0.5) = fmod(1/6, 0.5) = 1/6.
        // delta = 1/6 - 0 = 1/6, within +-0.25 so no wrap.
        // targetPosition = 2.0 + 1/6 = 13/6.
        const BeatGrid thisGrid = makeGrid(120.0, 0.0);
        const BeatGrid otherGrid = makeGrid(100.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 2.0, otherGrid, 2.0, 1.05f);

        REQUIRE(result.has_value());

        const float expectedRate = std::clamp(105.0f / 120.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

        const double expectedPosition =
            std::clamp(13.0 / 6.0, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
    }

    SECTION("other deck pitched down 5% (otherPlaybackRate = 0.95)")
    {
        // effectiveOtherBpm = 140 * 0.95 = 133, so playbackRate = 133/128.
        //
        // Phase step again must use the raw 140/128 ratio, not 133/128 - if it
        // had double-counted otherPlaybackRate, targetThisPhase would be
        // (133/128) * (1/7) = 0.1484375 instead of the correct
        // (140/128) * (1/7) = 5/32 = 0.15625, giving targetPosition 1.0859375
        // instead of the correct 1.09375 asserted here.
        //
        // thisBeatInterval = 60/128 = 0.46875s, otherBeatInterval = 60/140 = 3/7 s.
        // thisPhase = fmod(1.0, 0.46875) = 1.0 - 2*0.46875 = 0.0625.
        // otherPhase = fmod(1.0, 3/7) = 1.0 - 2*(3/7) = 1/7.
        // targetThisPhase = fmod((140/128) * (1/7), 0.46875) = fmod(5/32, 0.46875) = 5/32 = 0.15625.
        // delta = 0.15625 - 0.0625 = 0.09375, within +-0.234375 so no wrap.
        // targetPosition = 1.0 + 0.09375 = 1.09375.
        const BeatGrid thisGrid = makeGrid(128.0, 0.0);
        const BeatGrid otherGrid = makeGrid(140.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 1.0, otherGrid, 1.0, 0.95f);

        REQUIRE(result.has_value());

        const float expectedRate = std::clamp(133.0f / 128.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

        const double expectedPosition =
            std::clamp(1.09375, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
    }
}

TEST_CASE("computeBeatSync leaves position unchanged instead of clamping to the track boundary "
          "when the phase nudge would leave the track",
          "[BeatSync][end-of-track-fallback]")
{
    SECTION("nudge would push position past the end of the track")
    {
        // thisBeatInterval = 0.5s, otherBeatInterval = 60/90 = 2/3 s.
        // thisPhase = fmod(9.9, 0.5) = 9.9 - 19*0.5 = 0.4.
        // otherPhase = fmod(1/15, 2/3) = 1/15 (already smaller than the interval).
        // targetThisPhase = fmod((90/120) * (1/15), 0.5) = fmod(0.05, 0.5) = 0.05.
        // delta = 0.05 - 0.4 = -0.35, below -0.25 (half the interval), so
        // delta += 0.5 -> delta = 0.15.
        // targetPosition = 9.9 + 0.15 = 10.05, which is > thisDurationSeconds (10.0).
        // Per the corrected fallback, this must NOT clamp to 10.0 (the track
        // boundary) - it must fall back to the original, unmodified
        // thisCurrentPositionSeconds (9.9), while playbackRate still reflects
        // the tempo-match correction (90/120 = 0.75).
        const BeatGrid thisGrid = makeGrid(120.0, 0.0);
        const BeatGrid otherGrid = makeGrid(90.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 9.9, otherGrid, 1.0 / 15.0, 1.0f, 10.0);

        REQUIRE(result.has_value());

        const double expectedPosition = std::clamp(9.9, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
        // Explicitly not the track boundary - pins the "no clamp-to-boundary" fix.
        CHECK(result->positionSeconds != Catch::Approx(10.0).margin(1e-6));

        const float expectedRate = std::clamp(90.0f / 120.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));
    }

    SECTION("nudge would push position before the start of the track")
    {
        // thisBeatInterval = 0.6s, otherBeatInterval = 60/140 = 3/7 s.
        // thisPhase = fmod(0.1, 0.6) = 0.1 (already smaller than the interval).
        // otherPhase = fmod(5/14, 3/7) = 5/14 (already smaller than the interval).
        // targetThisPhase = fmod((140/100) * (5/14), 0.6) = fmod(0.5, 0.6) = 0.5.
        // delta = 0.5 - 0.1 = 0.4, above +0.3 (half the interval), so
        // delta -= 0.6 -> delta = -0.2.
        // targetPosition = 0.1 - 0.2 = -0.1, which is < 0.0.
        // Falls back to the original thisCurrentPositionSeconds (0.1) unchanged,
        // not clamped to 0.0, while playbackRate still reflects the tempo-match
        // correction (140/100 = 1.4).
        const BeatGrid thisGrid = makeGrid(100.0, 0.0);
        const BeatGrid otherGrid = makeGrid(140.0, 0.0);

        const auto result = computeBeatSync(thisGrid, 0.1, otherGrid, 5.0 / 14.0, 1.0f, 20.0);

        REQUIRE(result.has_value());

        const double expectedPosition = std::clamp(0.1, ranges::positionSecondsMin, ranges::positionSecondsMax);
        CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
        // Explicitly not the track boundary - pins the "no clamp-to-boundary" fix.
        CHECK(result->positionSeconds != Catch::Approx(0.0).margin(1e-6));

        const float expectedRate = std::clamp(140.0f / 100.0f, ranges::playbackRateMin, ranges::playbackRateMax);
        CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));
    }
}

TEST_CASE("computeBeatSync equal-BPM phase alignment is unchanged by the fix (regression guard)",
          "[BeatSync][equal-bpm]")
{
    // Small, single-case sanity check only: the fix must not change behaviour
    // when both decks share a BPM, since that was already the well-covered
    // path in the pre-existing suite. This is not new coverage, just a guard.
    //
    // thisBeatInterval = otherBeatInterval = 60/128 = 0.46875s.
    // thisPhase = fmod(10.0, 0.46875) = 10.0 - 21*0.46875 = 0.15625.
    // otherPhase = fmod(10.3, 0.46875) = 10.3 - 21*0.46875 = 0.45625.
    // targetThisPhase = fmod((128/128) * 0.45625, 0.46875) = 0.45625.
    // delta = 0.45625 - 0.15625 = 0.3, above +0.234375 (half the interval), so
    // delta -= 0.46875 -> delta = -0.16875.
    // targetPosition = 10.0 - 0.16875 = 9.83125.
    const BeatGrid thisGrid = makeGrid(128.0, 0.0);
    const BeatGrid otherGrid = makeGrid(128.0, 0.0);

    const auto result = computeBeatSync(thisGrid, 10.0, otherGrid, 10.3, 1.0f);

    REQUIRE(result.has_value());

    const float expectedRate = std::clamp(1.0f, ranges::playbackRateMin, ranges::playbackRateMax);
    CHECK(result->playbackRate == Catch::Approx(expectedRate).margin(1e-5));

    const double expectedPosition = std::clamp(9.83125, ranges::positionSecondsMin, ranges::positionSecondsMax);
    CHECK(result->positionSeconds == Catch::Approx(expectedPosition).margin(1e-6));
}
