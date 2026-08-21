// Regression coverage for model/FullResyncDelta.h, extracted from app/MainComponent
// after a real bug: claiming control while already playing at a diverged local
// position/rate (e.g. solo, unsynced, before claiming) never told anyone - peers
// only caught up field-by-field, as each was later touched by a UI action. app/
// itself is host-checklist-only (05-testing.md), so the fix pulled the "every
// field, present" mapping into this pure, Catch2-testable function.

#include <catch2/catch_test_macros.hpp>

#include "model/FullResyncDelta.h"

TEST_CASE("FullResyncDelta: every field is present, carrying the source state's values",
          "[fullresyncdelta]")
{
    djapp::PlaybackState state;
    state.trackId = "demo1";
    state.playing = true;
    state.positionSeconds = 12.5;
    state.gain = 0.75f;
    state.playbackRate = 1.25f;
    state.pitchOffsetSemitones = -2.0f;
    state.loop = djapp::LoopPoints{3.0, 6.0};

    const auto delta = djapp::fullResyncDelta(djapp::DeckId::B, state);

    REQUIRE(delta.deck == djapp::DeckId::B);
    REQUIRE(delta.trackId.has_value());
    REQUIRE(*delta.trackId == "demo1");
    REQUIRE(delta.playing.has_value());
    REQUIRE(*delta.playing == true);
    REQUIRE(delta.positionSeconds.has_value());
    REQUIRE(*delta.positionSeconds == 12.5);
    REQUIRE(delta.gain.has_value());
    REQUIRE(*delta.gain == 0.75f);
    REQUIRE(delta.playbackRate.has_value());
    REQUIRE(*delta.playbackRate == 1.25f);
    REQUIRE(delta.pitchOffsetSemitones.has_value());
    REQUIRE(*delta.pitchOffsetSemitones == -2.0f);
    REQUIRE(delta.loop.has_value());
    REQUIRE(delta.loop->has_value());
    REQUIRE((*delta.loop)->inSeconds == 3.0);
    REQUIRE((*delta.loop)->outSeconds == 6.0);
}

TEST_CASE("FullResyncDelta: no track loaded carries an explicit-null trackId and loop, not absent",
          "[fullresyncdelta]")
{
    djapp::PlaybackState state; // defaults: empty trackId, no loop

    const auto delta = djapp::fullResyncDelta(djapp::DeckId::A, state);

    REQUIRE(delta.trackId.has_value());
    REQUIRE(delta.trackId->isEmpty());
    REQUIRE(delta.loop.has_value());
    REQUIRE_FALSE(delta.loop->has_value());
    REQUIRE_FALSE(delta.empty());
}
