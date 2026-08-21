#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "model/CrossfaderCurve.h"

using namespace Catch::Matchers;
using namespace djapp;

TEST_CASE("position 0.0 gives full deck A and silent deck B", "[CrossfaderCurve]")
{
    CrossfaderGains gains = equalPowerCrossfade(0.0f);
    REQUIRE_THAT(gains.gainA, WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(gains.gainB, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("position 1.0 gives full deck B and silent deck A", "[CrossfaderCurve]")
{
    CrossfaderGains gains = equalPowerCrossfade(1.0f);
    REQUIRE_THAT(gains.gainA, WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(gains.gainB, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("position 0.5 gives equal gains of one over sqrt two", "[CrossfaderCurve]")
{
    CrossfaderGains gains = equalPowerCrossfade(0.5f);
    REQUIRE_THAT(gains.gainA, WithinAbs(0.70710678f, 1e-5f));
    REQUIRE_THAT(gains.gainB, WithinAbs(0.70710678f, 1e-5f));
}

TEST_CASE("equal power property holds across the range of positions", "[CrossfaderCurve]")
{
    for (float position : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        CrossfaderGains gains = equalPowerCrossfade(position);
        float totalPower = gains.gainA * gains.gainA + gains.gainB * gains.gainB;
        REQUIRE_THAT(totalPower, WithinAbs(1.0f, 1e-5f));
    }
}

TEST_CASE("gainA never increases and gainB never decreases as position increases", "[CrossfaderCurve]")
{
    const float positions[] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
    CrossfaderGains previous = equalPowerCrossfade(positions[0]);

    for (size_t i = 1; i < std::size(positions); ++i)
    {
        CrossfaderGains current = equalPowerCrossfade(positions[i]);
        REQUIRE(current.gainA <= previous.gainA + 1e-5f);
        REQUIRE(current.gainB >= previous.gainB - 1e-5f);
        previous = current;
    }
}

TEST_CASE("position below zero clamps to the same result as position zero", "[CrossfaderCurve]")
{
    CrossfaderGains clamped = equalPowerCrossfade(-0.5f);
    CrossfaderGains atZero = equalPowerCrossfade(0.0f);
    REQUIRE_THAT(clamped.gainA, WithinAbs(atZero.gainA, 1e-5f));
    REQUIRE_THAT(clamped.gainB, WithinAbs(atZero.gainB, 1e-5f));
}

TEST_CASE("position above one clamps to the same result as position one", "[CrossfaderCurve]")
{
    CrossfaderGains clamped = equalPowerCrossfade(1.5f);
    CrossfaderGains atOne = equalPowerCrossfade(1.0f);
    REQUIRE_THAT(clamped.gainA, WithinAbs(atOne.gainA, 1e-5f));
    REQUIRE_THAT(clamped.gainB, WithinAbs(atOne.gainB, 1e-5f));
}
