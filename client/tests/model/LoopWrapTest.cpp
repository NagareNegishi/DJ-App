// Black-box tests for model/LoopWrap.h, derived purely from the spec handed
// to this agent (no implementation source was read).
//
// wrapPositionWithinRange(position, rangeStart, rangeEnd): unit-agnostic
// (samples or seconds); wraps position into [rangeStart, rangeEnd) once
// position >= rangeEnd and rangeEnd > rangeStart, otherwise returns position
// unchanged.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "model/LoopWrap.h"

TEST_CASE("LoopWrap: position below rangeEnd is returned unchanged", "[loopwrap]")
{
    SECTION("position inside the range")
    {
        REQUIRE(djapp::wrapPositionWithinRange(6.0, 5.0, 8.0) == 6.0);
    }

    SECTION("position before rangeStart (never clamped at the low end)")
    {
        REQUIRE(djapp::wrapPositionWithinRange(1.0, 5.0, 8.0) == 1.0);
    }

    SECTION("negative position before rangeStart")
    {
        REQUIRE(djapp::wrapPositionWithinRange(-100.0, 5.0, 8.0) == -100.0);
    }
}

TEST_CASE("LoopWrap: position at or past rangeEnd wraps via the fmod formula", "[loopwrap]")
{
    SECTION("position exactly at rangeEnd wraps to rangeStart")
    {
        const double result = djapp::wrapPositionWithinRange(8.0, 5.0, 8.0);
        REQUIRE(result == 5.0 + std::fmod(8.0 - 5.0, 3.0));
        REQUIRE(result == 5.0);
    }

    SECTION("position past rangeEnd wraps to the fmod-derived point, never reaching rangeEnd")
    {
        const double result = djapp::wrapPositionWithinRange(13.0, 5.0, 8.0);
        REQUIRE(result == 5.0 + std::fmod(13.0 - 5.0, 3.0));
        REQUIRE(result != 8.0);
        REQUIRE(result < 8.0);
    }
}

TEST_CASE("LoopWrap: position several range-lengths past rangeEnd still wraps correctly", "[loopwrap]")
{
    // 5 range-lengths (15.0) past rangeStart plus a partial remainder, well
    // beyond a single wrap.
    const double position = 5.0 + (5.0 * 3.0) + 1.5; // = 21.5, range length 3.0
    const double result = djapp::wrapPositionWithinRange(position, 5.0, 8.0);
    REQUIRE(result == 5.0 + std::fmod(position - 5.0, 3.0));
    REQUIRE(result >= 5.0);
    REQUIRE(result < 8.0);
}

TEST_CASE("LoopWrap: degenerate range (rangeEnd <= rangeStart) leaves position unchanged, no division by zero",
          "[loopwrap]")
{
    SECTION("rangeEnd < rangeStart")
    {
        REQUIRE(djapp::wrapPositionWithinRange(13.0, 8.0, 5.0) == 13.0);
    }

    SECTION("rangeEnd == rangeStart")
    {
        REQUIRE(djapp::wrapPositionWithinRange(13.0, 5.0, 5.0) == 13.0);
    }

    SECTION("rangeEnd == rangeStart, position also equal to both")
    {
        REQUIRE(djapp::wrapPositionWithinRange(5.0, 5.0, 5.0) == 5.0);
    }
}

TEST_CASE("LoopWrap: position exactly equal to rangeStart is returned unchanged", "[loopwrap]")
{
    REQUIRE(djapp::wrapPositionWithinRange(5.0, 5.0, 8.0) == 5.0);
}

// --- White-box numeric-edge cases, added after reading the fmod-based implementation. ---

TEST_CASE("LoopWrap: position an exact integer multiple of the range length past rangeEnd wraps "
          "to exactly rangeStart",
          "[loopwrap][whitebox]")
{
    // 3 whole range-lengths (15.0) past rangeStart, landing squarely on a wrap seam
    // rather than the partial-remainder case the black-box suite already covers.
    const double result = djapp::wrapPositionWithinRange(15.0, 0.0, 5.0);
    REQUIRE(result == 0.0);
}

TEST_CASE("LoopWrap: range straddling zero (negative rangeStart, positive rangeEnd) wraps within bounds",
          "[loopwrap][whitebox]")
{
    const double position = 100.0;
    const double result = djapp::wrapPositionWithinRange(position, -3.0, 4.0);
    REQUIRE(result == -3.0 + std::fmod(position - -3.0, 7.0));
    REQUIRE(result >= -3.0);
    REQUIRE(result < 4.0);
}

TEST_CASE("LoopWrap: fully negative range (both bounds below zero) wraps within bounds",
          "[loopwrap][whitebox]")
{
    const double position = -1.0; // past rangeEnd of -4.0
    const double result = djapp::wrapPositionWithinRange(position, -10.0, -4.0);
    REQUIRE(result == -10.0 + std::fmod(position - -10.0, 6.0));
    REQUIRE(result >= -10.0);
    REQUIRE(result < -4.0);
}

TEST_CASE("LoopWrap: sub-sample-scale range (length far below 1.0) still wraps within bounds, "
          "no NaN or infinity",
          "[loopwrap][whitebox]")
{
    const double rangeStart = 1.0;
    const double rangeEnd = 1.0 + 1e-6;
    const double result = djapp::wrapPositionWithinRange(5.0, rangeStart, rangeEnd);
    REQUIRE(std::isfinite(result));
    REQUIRE(result >= rangeStart);
    REQUIRE(result < rangeEnd);
}

TEST_CASE("LoopWrap: large-magnitude rangeStart/rangeEnd (values far from zero) still wraps within "
          "bounds, no NaN or infinity",
          "[loopwrap][whitebox]")
{
    const double rangeStart = 1.0e10;
    const double rangeEnd = 1.0e10 + 5.0;
    const double position = 1.0e10 + 1.0e6 + 2.5; // many range-lengths past rangeEnd
    const double result = djapp::wrapPositionWithinRange(position, rangeStart, rangeEnd);
    REQUIRE(std::isfinite(result));
    REQUIRE(result >= rangeStart);
    REQUIRE(result < rangeEnd);
}
