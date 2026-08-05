// White-box tests for model/Ranges.h, written after reading the
// implementation. clamp() is a set of thin std::clamp(v, lo, hi) wrappers;
// the black-box suite already pins "below min -> min", "above max -> max",
// "in range -> unchanged" and "exactly at boundary -> unchanged" for every
// field. What it can't know from the spec alone:
//   - the exact ULP-level boundary behavior (one representable step inside
//     vs. outside each bound), which would catch a `<` vs `<=` slip that a
//     coarser "-5.0 clamps to 0.0" test cannot;
//   - the public djapp::ranges::clamp(LoopPoints&) overload, called
//     directly rather than only indirectly through PlaybackState/StateDelta;
//   - std::clamp's behavior when handed a value clamp() itself does not
//     promise to reject (NaN), since Ranges.h's own doc comment says range
//     enforcement assumes an already-validated (finite, well-typed) input
//     and leaves finiteness to Serialization.h's parse step.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>

#include "model/Ranges.h"
#include "model/Types.h"

TEST_CASE("Ranges: one ULP inside each bound is left unchanged", "[ranges][edge]")
{
    SECTION("positionSeconds one ULP above min, one ULP below max")
    {
        djapp::PlaybackState s;
        s.positionSeconds = std::nextafter(djapp::ranges::positionSecondsMin, 1.0);
        auto expectMin = s.positionSeconds;
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == expectMin);

        s.positionSeconds = std::nextafter(djapp::ranges::positionSecondsMax, 0.0);
        auto expectMax = s.positionSeconds;
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == expectMax);
    }

    SECTION("gain one ULP inside each bound")
    {
        djapp::PlaybackState s;
        s.gain = std::nextafter(djapp::ranges::gainMin, 1.0f);
        auto expectMin = s.gain;
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == expectMin);

        s.gain = std::nextafter(djapp::ranges::gainMax, 0.0f);
        auto expectMax = s.gain;
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == expectMax);
    }

    SECTION("playbackRate one ULP inside each bound")
    {
        djapp::PlaybackState s;
        s.playbackRate = std::nextafter(djapp::ranges::playbackRateMin, 1.0f);
        auto expectMin = s.playbackRate;
        djapp::ranges::clamp(s);
        REQUIRE(s.playbackRate == expectMin);

        s.playbackRate = std::nextafter(djapp::ranges::playbackRateMax, 0.0f);
        auto expectMax = s.playbackRate;
        djapp::ranges::clamp(s);
        REQUIRE(s.playbackRate == expectMax);
    }

    SECTION("pitchOffsetSemitones one ULP inside each bound")
    {
        djapp::PlaybackState s;
        s.pitchOffsetSemitones = std::nextafter(djapp::ranges::pitchOffsetSemitonesMin, 0.0f);
        auto expectMin = s.pitchOffsetSemitones;
        djapp::ranges::clamp(s);
        REQUIRE(s.pitchOffsetSemitones == expectMin);

        s.pitchOffsetSemitones = std::nextafter(djapp::ranges::pitchOffsetSemitonesMax, 0.0f);
        auto expectMax = s.pitchOffsetSemitones;
        djapp::ranges::clamp(s);
        REQUIRE(s.pitchOffsetSemitones == expectMax);
    }
}

TEST_CASE("Ranges: one ULP outside each bound clamps to the bound", "[ranges][edge]")
{
    SECTION("positionSeconds one ULP below min, one ULP above max")
    {
        djapp::PlaybackState s;
        s.positionSeconds = std::nextafter(djapp::ranges::positionSecondsMin, -1.0);
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == djapp::ranges::positionSecondsMin);

        s.positionSeconds = std::nextafter(djapp::ranges::positionSecondsMax, std::numeric_limits<double>::max());
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == djapp::ranges::positionSecondsMax);
    }

    SECTION("gain one ULP below min, one ULP above max")
    {
        djapp::PlaybackState s;
        s.gain = std::nextafter(djapp::ranges::gainMin, -1.0f);
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == djapp::ranges::gainMin);

        s.gain = std::nextafter(djapp::ranges::gainMax, std::numeric_limits<float>::max());
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == djapp::ranges::gainMax);
    }
}

TEST_CASE("Ranges: -0.0 compares equal to the 0.0 lower bound and is left unchanged", "[ranges][edge]")
{
    // IEEE 754: -0.0 < 0.0 is false, so std::clamp's "v < lo" branch does not
    // fire and -0.0 passes through with its sign bit intact. This documents
    // the actual (not obviously-buggy) behavior at this specific boundary,
    // since positionSecondsMin/gainMin/loopSecondsMin are all 0.0.
    djapp::PlaybackState s;
    s.positionSeconds = -0.0;
    s.gain = -0.0f;
    djapp::ranges::clamp(s);

    REQUIRE(s.positionSeconds == 0.0);
    REQUIRE(std::signbit(s.positionSeconds));
    REQUIRE(s.gain == 0.0f);
    REQUIRE(std::signbit(s.gain));
}

TEST_CASE("Ranges: clamp(LoopPoints&) is usable directly, not only via PlaybackState/StateDelta", "[ranges][edge]")
{
    djapp::LoopPoints loop{-5.0, 999999.0};
    djapp::ranges::clamp(loop);
    REQUIRE(loop.inSeconds == djapp::ranges::loopSecondsMin);
    REQUIRE(loop.outSeconds == djapp::ranges::loopSecondsMax);
}

TEST_CASE("Ranges: clamp() called directly on a NaN value leaves it as NaN (not rejected)", "[ranges][edge]")
{
    // Ranges.h's own doc comment scopes clamp() to "valid shape" input and
    // defers finiteness rejection to Serialization.h's parse step. This
    // pins that division of labor: clamp() does not itself guard against a
    // NaN reaching it by a path other than fromVar (e.g. a StateDelta built
    // directly by C++ code, not parsed from the wire). Both of std::clamp's
    // internal comparisons (v < lo, hi < v) are false for NaN, so std::clamp
    // returns v unchanged -- the NaN survives the "defense-in-depth" pass.
    djapp::PlaybackState s;
    s.gain = std::numeric_limits<float>::quiet_NaN();
    djapp::ranges::clamp(s);
    REQUIRE(std::isnan(s.gain));
}
