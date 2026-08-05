// Black-box tests for model/Ranges.h, derived purely from the spec handed to
// this agent (no implementation source was read).
//
// Range table under test (min/max inclusive unless noted):
//   positionSeconds          [0, 86400]
//   gain                     [0.0, 2.0]
//   playbackRate             [0.5, 2.0]
//   pitchOffsetSemitones     [-12, 12]
//   loop.inSeconds/outSeconds [0, 86400]  (clamped independently; if clamping
//                              would leave inSeconds >= outSeconds, the whole
//                              loop is cleared instead of kept degenerate)

#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "model/Ranges.h"
#include "model/Types.h"

namespace
{

djapp::PlaybackState inRangeState()
{
    djapp::PlaybackState s;
    s.trackId = "track_01";
    s.playing = true;
    s.positionSeconds = 100.0;
    s.gain = 1.0f;
    s.playbackRate = 1.0f;
    s.pitchOffsetSemitones = 0.0f;
    s.loop = djapp::LoopPoints{10.0, 20.0};
    return s;
}

} // namespace

TEST_CASE("Ranges: PlaybackState fields below minimum clamp to minimum", "[ranges]")
{
    SECTION("positionSeconds")
    {
        auto s = inRangeState();
        s.positionSeconds = -5.0;
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == 0.0);
    }

    SECTION("gain")
    {
        auto s = inRangeState();
        s.gain = -0.5f;
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == 0.0f);
    }

    SECTION("playbackRate")
    {
        auto s = inRangeState();
        s.playbackRate = 0.1f;
        djapp::ranges::clamp(s);
        REQUIRE(s.playbackRate == 0.5f);
    }

    SECTION("pitchOffsetSemitones")
    {
        auto s = inRangeState();
        s.pitchOffsetSemitones = -20.0f;
        djapp::ranges::clamp(s);
        REQUIRE(s.pitchOffsetSemitones == -12.0f);
    }

    SECTION("loop.inSeconds below minimum, loop.outSeconds unaffected")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{-10.0, 50.0};
        djapp::ranges::clamp(s);
        REQUIRE(s.loop.has_value());
        REQUIRE(s.loop->inSeconds == 0.0);
        REQUIRE(s.loop->outSeconds == 50.0);
    }
}

TEST_CASE("Ranges: PlaybackState fields above maximum clamp to maximum", "[ranges]")
{
    SECTION("positionSeconds")
    {
        auto s = inRangeState();
        s.positionSeconds = 999999.0;
        djapp::ranges::clamp(s);
        REQUIRE(s.positionSeconds == 86400.0);
    }

    SECTION("gain")
    {
        auto s = inRangeState();
        s.gain = 5.0f;
        djapp::ranges::clamp(s);
        REQUIRE(s.gain == 2.0f);
    }

    SECTION("playbackRate")
    {
        auto s = inRangeState();
        s.playbackRate = 3.0f;
        djapp::ranges::clamp(s);
        REQUIRE(s.playbackRate == 2.0f);
    }

    SECTION("pitchOffsetSemitones")
    {
        auto s = inRangeState();
        s.pitchOffsetSemitones = 50.0f;
        djapp::ranges::clamp(s);
        REQUIRE(s.pitchOffsetSemitones == 12.0f);
    }

    SECTION("loop.outSeconds above maximum, loop.inSeconds unaffected")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{50.0, 999999.0};
        djapp::ranges::clamp(s);
        REQUIRE(s.loop.has_value());
        REQUIRE(s.loop->inSeconds == 50.0);
        REQUIRE(s.loop->outSeconds == 86400.0);
    }
}

TEST_CASE("Ranges: PlaybackState values already inside range are left unchanged", "[ranges]")
{
    auto s = inRangeState();
    auto before = s;
    djapp::ranges::clamp(s);

    REQUIRE(s.positionSeconds == before.positionSeconds);
    REQUIRE(s.gain == before.gain);
    REQUIRE(s.playbackRate == before.playbackRate);
    REQUIRE(s.pitchOffsetSemitones == before.pitchOffsetSemitones);
    REQUIRE(s.loop.has_value());
    REQUIRE(s.loop->inSeconds == before.loop->inSeconds);
    REQUIRE(s.loop->outSeconds == before.loop->outSeconds);
}

TEST_CASE("Ranges: PlaybackState values exactly at a boundary are left unchanged", "[ranges]")
{
    SECTION("positionSeconds at min (0) and max (86400)")
    {
        auto sMin = inRangeState();
        sMin.positionSeconds = 0.0;
        djapp::ranges::clamp(sMin);
        REQUIRE(sMin.positionSeconds == 0.0);

        auto sMax = inRangeState();
        sMax.positionSeconds = 86400.0;
        djapp::ranges::clamp(sMax);
        REQUIRE(sMax.positionSeconds == 86400.0);
    }

    SECTION("gain at min (0.0) and max (2.0)")
    {
        auto sMin = inRangeState();
        sMin.gain = 0.0f;
        djapp::ranges::clamp(sMin);
        REQUIRE(sMin.gain == 0.0f);

        auto sMax = inRangeState();
        sMax.gain = 2.0f;
        djapp::ranges::clamp(sMax);
        REQUIRE(sMax.gain == 2.0f);
    }

    SECTION("playbackRate at min (0.5) and max (2.0)")
    {
        auto sMin = inRangeState();
        sMin.playbackRate = 0.5f;
        djapp::ranges::clamp(sMin);
        REQUIRE(sMin.playbackRate == 0.5f);

        auto sMax = inRangeState();
        sMax.playbackRate = 2.0f;
        djapp::ranges::clamp(sMax);
        REQUIRE(sMax.playbackRate == 2.0f);
    }

    SECTION("pitchOffsetSemitones at min (-12) and max (12)")
    {
        auto sMin = inRangeState();
        sMin.pitchOffsetSemitones = -12.0f;
        djapp::ranges::clamp(sMin);
        REQUIRE(sMin.pitchOffsetSemitones == -12.0f);

        auto sMax = inRangeState();
        sMax.pitchOffsetSemitones = 12.0f;
        djapp::ranges::clamp(sMax);
        REQUIRE(sMax.pitchOffsetSemitones == 12.0f);
    }

    SECTION("loop.inSeconds/outSeconds at min (0) and max (86400)")
    {
        // Each endpoint tested at its own boundary while the other stays a
        // distinct in-range value, so the result remains a valid (in < out)
        // loop rather than collapsing (a collapsed loop is its own test case,
        // see the "cleared instead of kept degenerate" TEST_CASE below).
        auto sMin = inRangeState();
        sMin.loop = djapp::LoopPoints{0.0, 50.0};
        djapp::ranges::clamp(sMin);
        REQUIRE(sMin.loop.has_value());
        REQUIRE(sMin.loop->inSeconds == 0.0);
        REQUIRE(sMin.loop->outSeconds == 50.0);

        auto sMax = inRangeState();
        sMax.loop = djapp::LoopPoints{50.0, 86400.0};
        djapp::ranges::clamp(sMax);
        REQUIRE(sMax.loop.has_value());
        REQUIRE(sMax.loop->inSeconds == 50.0);
        REQUIRE(sMax.loop->outSeconds == 86400.0);
    }
}

TEST_CASE("Ranges: clamp(StateDelta&) only touches present fields, out-of-range in each", "[ranges]")
{
    SECTION("only positionSeconds present")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.positionSeconds = -5.0;
        djapp::ranges::clamp(d);

        REQUIRE(d.positionSeconds.has_value());
        REQUIRE(*d.positionSeconds == 0.0);
        REQUIRE_FALSE(d.trackId.has_value());
        REQUIRE_FALSE(d.playing.has_value());
        REQUIRE_FALSE(d.gain.has_value());
        REQUIRE_FALSE(d.playbackRate.has_value());
        REQUIRE_FALSE(d.pitchOffsetSemitones.has_value());
        REQUIRE_FALSE(d.loop.has_value());
    }

    SECTION("only gain present")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.gain = 5.0f;
        djapp::ranges::clamp(d);

        REQUIRE(d.gain.has_value());
        REQUIRE(*d.gain == 2.0f);
        REQUIRE_FALSE(d.positionSeconds.has_value());
        REQUIRE_FALSE(d.playbackRate.has_value());
        REQUIRE_FALSE(d.pitchOffsetSemitones.has_value());
        REQUIRE_FALSE(d.loop.has_value());
    }

    SECTION("only playbackRate present")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.playbackRate = 0.1f;
        djapp::ranges::clamp(d);

        REQUIRE(d.playbackRate.has_value());
        REQUIRE(*d.playbackRate == 0.5f);
        REQUIRE_FALSE(d.gain.has_value());
        REQUIRE_FALSE(d.positionSeconds.has_value());
        REQUIRE_FALSE(d.pitchOffsetSemitones.has_value());
        REQUIRE_FALSE(d.loop.has_value());
    }

    SECTION("only pitchOffsetSemitones present")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.pitchOffsetSemitones = 50.0f;
        djapp::ranges::clamp(d);

        REQUIRE(d.pitchOffsetSemitones.has_value());
        REQUIRE(*d.pitchOffsetSemitones == 12.0f);
        REQUIRE_FALSE(d.gain.has_value());
        REQUIRE_FALSE(d.positionSeconds.has_value());
        REQUIRE_FALSE(d.playbackRate.has_value());
        REQUIRE_FALSE(d.loop.has_value());
    }

    SECTION("only loop present (outer and inner engaged), both fields clamped")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.loop = std::optional<djapp::LoopPoints>(djapp::LoopPoints{-10.0, 999999.0});
        djapp::ranges::clamp(d);

        REQUIRE(d.loop.has_value());
        REQUIRE(d.loop->has_value());
        REQUIRE((*d.loop)->inSeconds == 0.0);
        REQUIRE((*d.loop)->outSeconds == 86400.0);
        REQUIRE_FALSE(d.gain.has_value());
        REQUIRE_FALSE(d.positionSeconds.has_value());
        REQUIRE_FALSE(d.playbackRate.has_value());
        REQUIRE_FALSE(d.pitchOffsetSemitones.has_value());
    }
}

TEST_CASE("Ranges: clamp(StateDelta&) leaves an absent field absent, not materialized", "[ranges]")
{
    djapp::StateDelta d;
    d.deck = djapp::DeckId::B;
    // Every optional field left default-constructed (absent).
    djapp::ranges::clamp(d);

    REQUIRE_FALSE(d.trackId.has_value());
    REQUIRE_FALSE(d.playing.has_value());
    REQUIRE_FALSE(d.positionSeconds.has_value());
    REQUIRE_FALSE(d.gain.has_value());
    REQUIRE_FALSE(d.playbackRate.has_value());
    REQUIRE_FALSE(d.pitchOffsetSemitones.has_value());
    REQUIRE_FALSE(d.loop.has_value());
}

TEST_CASE("Ranges: clamp(StateDelta&) with loop outer-present/inner-absent (explicit clear) is left as a clear, not "
          "fabricated",
          "[ranges]")
{
    djapp::StateDelta d;
    d.deck = djapp::DeckId::A;
    d.loop = std::optional<djapp::LoopPoints>(std::nullopt); // outer engaged, inner empty
    djapp::ranges::clamp(d);

    REQUIRE(d.loop.has_value());
    REQUIRE_FALSE(d.loop->has_value());
}

TEST_CASE("Ranges: loop endpoints clamp independently when the result stays a valid (in < out) loop", "[ranges]")
{
    SECTION("PlaybackState.loop: inSeconds in range, outSeconds clamps down but stays above inSeconds")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{50000.0, 999999.0}; // outSeconds clamps to 86400, still > inSeconds
        djapp::ranges::clamp(s);

        REQUIRE(s.loop.has_value());
        REQUIRE(s.loop->inSeconds == 50000.0);
        REQUIRE(s.loop->outSeconds == 86400.0);
    }

    SECTION("StateDelta.loop: same independent clamp, stays valid")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.loop = std::optional<djapp::LoopPoints>(djapp::LoopPoints{50000.0, 999999.0});
        djapp::ranges::clamp(d);

        REQUIRE(d.loop.has_value());
        REQUIRE(d.loop->has_value());
        REQUIRE((*d.loop)->inSeconds == 50000.0);
        REQUIRE((*d.loop)->outSeconds == 86400.0);
    }
}

TEST_CASE("Ranges: a loop that would clamp to inSeconds >= outSeconds is cleared instead of kept degenerate",
          "[ranges]")
{
    SECTION("PlaybackState.loop: both endpoints clamp to the same minimum -> cleared")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{-10.0, -20.0}; // both clamp to 0.0
        djapp::ranges::clamp(s);
        REQUIRE_FALSE(s.loop.has_value());
    }

    SECTION("PlaybackState.loop: both endpoints clamp to the same maximum -> cleared")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{90000.0, 100000.0}; // both clamp to 86400.0
        djapp::ranges::clamp(s);
        REQUIRE_FALSE(s.loop.has_value());
    }

    SECTION("PlaybackState.loop: inSeconds in range, outSeconds clamps below it -> cleared")
    {
        auto s = inRangeState();
        s.loop = djapp::LoopPoints{50000.0, -10.0}; // outSeconds clamps to 0.0, now < inSeconds
        djapp::ranges::clamp(s);
        REQUIRE_FALSE(s.loop.has_value());
    }

    SECTION("StateDelta.loop: collapse clears the inner optional, leaving the outer engaged "
            "(an explicit \"clear the loop\" delta, not a dropped field)")
    {
        djapp::StateDelta d;
        d.deck = djapp::DeckId::A;
        d.loop = std::optional<djapp::LoopPoints>(djapp::LoopPoints{90000.0, 100000.0});
        djapp::ranges::clamp(d);

        REQUIRE(d.loop.has_value());        // outer still present
        REQUIRE_FALSE(d.loop->has_value()); // inner cleared
    }
}
