// Black-box tests for model/Serialization.h, derived purely from the spec
// handed to this agent (no implementation source was read).
//
// Two API-shape assumptions had to be made to get this file to compile at
// all; both are called out in the test-writer's report as guesses that need
// confirming against the real implementation:
//
//  1. `fromVar` is listed in the spec as two free functions with identical
//     parameter lists (`const juce::var&`) but different return types
//     (`Result<PlaybackState>` / `Result<StateDelta>`). Plain function
//     overloading cannot distinguish on return type alone, so the only
//     standard-C++ shape that matches the stated signatures is a function
//     template `template <typename T> Result<T> fromVar(const juce::var&)`
//     invoked with an explicit template argument. This file therefore calls
//     `djapp::fromVar<djapp::PlaybackState>(v)` / `djapp::fromVar<djapp::StateDelta>(v)`.
//
//  2. `Result<T>`'s exact member names are left to the implementer's choice
//     by the spec. This file assumes an `std::optional`-like surface:
//     `explicit operator bool() const` for success, and `operator*() const`
//     for extracting the value on success. Both uses are funneled through the
//     two helpers below (`isOk` / `unwrap`) so only these two lines need to
//     change if the real API differs.
//
// JSON key names used when hand-building `juce::var` trees (trackId,
// playing, positionSeconds, gain, playbackRate, pitchOffsetSemitones, loop,
// inSeconds, outSeconds, deck) are taken verbatim from the spec's own prose
// examples (e.g. `"gain":"abc"`, `"playing":1`,
// `{"inSeconds":1,"outSeconds":2,"x":9}`), not guessed.

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>

#include "model/Types.h"
#include "model/Serialization.h"

namespace {

template <typename R>
bool isOk(const R& r) { return static_cast<bool>(r); }

template <typename R>
auto unwrap(const R& r) { return *r; }

djapp::PlaybackState makeValidState(bool withLoop)
{
    djapp::PlaybackState s;
    s.trackId = "track_01";
    s.playing = true;
    s.positionSeconds = 123.456;
    s.gain = 0.75f;
    s.playbackRate = 1.25f;
    s.pitchOffsetSemitones = -3.5f;
    if (withLoop)
        s.loop = djapp::LoopPoints{10.0, 20.0};
    return s;
}

djapp::StateDelta makeValidDelta(bool withLoop)
{
    djapp::StateDelta d;
    d.deck = djapp::DeckId::B;
    d.trackId = "abc";
    d.playing = true;
    d.positionSeconds = 42.0;
    d.gain = 1.5f;
    d.playbackRate = 0.8f;
    d.pitchOffsetSemitones = 2.0f;
    if (withLoop)
        d.loop = std::optional<djapp::LoopPoints>(djapp::LoopPoints{1.0, 2.0});
    return d;
}

} // namespace

TEST_CASE("Serialization: PlaybackState round-trips through toVar/fromVar", "[serialization]")
{
    SECTION("with loop set")
    {
        auto original = makeValidState(true);
        auto result = djapp::fromVar<djapp::PlaybackState>(djapp::toVar(original));
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);

        REQUIRE(parsed.trackId == original.trackId);
        REQUIRE(parsed.playing == original.playing);
        REQUIRE(parsed.positionSeconds == original.positionSeconds);
        REQUIRE(parsed.gain == original.gain);
        REQUIRE(parsed.playbackRate == original.playbackRate);
        REQUIRE(parsed.pitchOffsetSemitones == original.pitchOffsetSemitones);
        REQUIRE(parsed.loop.has_value());
        REQUIRE(parsed.loop->inSeconds == original.loop->inSeconds);
        REQUIRE(parsed.loop->outSeconds == original.loop->outSeconds);
    }

    SECTION("without loop")
    {
        auto original = makeValidState(false);
        auto result = djapp::fromVar<djapp::PlaybackState>(djapp::toVar(original));
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);

        REQUIRE(parsed.trackId == original.trackId);
        REQUIRE(parsed.playing == original.playing);
        REQUIRE(parsed.positionSeconds == original.positionSeconds);
        REQUIRE(parsed.gain == original.gain);
        REQUIRE(parsed.playbackRate == original.playbackRate);
        REQUIRE(parsed.pitchOffsetSemitones == original.pitchOffsetSemitones);
        REQUIRE_FALSE(parsed.loop.has_value());
    }
}

TEST_CASE("Serialization: StateDelta round-trips through toVar/fromVar", "[serialization]")
{
    SECTION("with loop present and set")
    {
        auto original = makeValidDelta(true);
        auto result = djapp::fromVar<djapp::StateDelta>(djapp::toVar(original));
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);

        REQUIRE(parsed.deck == original.deck);
        REQUIRE(parsed.trackId.has_value());
        REQUIRE(*parsed.trackId == *original.trackId);
        REQUIRE(parsed.playing.has_value());
        REQUIRE(*parsed.playing == *original.playing);
        REQUIRE(parsed.positionSeconds.has_value());
        REQUIRE(*parsed.positionSeconds == *original.positionSeconds);
        REQUIRE(parsed.gain.has_value());
        REQUIRE(*parsed.gain == *original.gain);
        REQUIRE(parsed.playbackRate.has_value());
        REQUIRE(*parsed.playbackRate == *original.playbackRate);
        REQUIRE(parsed.pitchOffsetSemitones.has_value());
        REQUIRE(*parsed.pitchOffsetSemitones == *original.pitchOffsetSemitones);
        REQUIRE(parsed.loop.has_value());
        REQUIRE(parsed.loop->has_value());
        REQUIRE((*parsed.loop)->inSeconds == (*original.loop)->inSeconds);
        REQUIRE((*parsed.loop)->outSeconds == (*original.loop)->outSeconds);
    }

    SECTION("with loop field absent")
    {
        auto original = makeValidDelta(false);
        auto result = djapp::fromVar<djapp::StateDelta>(djapp::toVar(original));
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);

        REQUIRE(parsed.deck == original.deck);
        REQUIRE_FALSE(parsed.loop.has_value());
    }
}

TEST_CASE("Serialization: unknown top-level fields are rejected", "[serialization]")
{
    SECTION("PlaybackState with an extra top-level key fails")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("unknownField", 42);

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("StateDelta with an extra top-level key fails")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("unknownField", 42);

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: unknown fields inside a loop object are rejected", "[serialization]")
{
    SECTION("PlaybackState.loop with an extra key fails")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        auto loopVar = obj->getProperty("loop");
        auto* loopObj = loopVar.getDynamicObject();
        REQUIRE(loopObj != nullptr);
        loopObj->setProperty("x", 9);

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("StateDelta.loop with an extra key fails")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        auto loopVar = obj->getProperty("loop");
        auto* loopObj = loopVar.getDynamicObject();
        REQUIRE(loopObj != nullptr);
        loopObj->setProperty("x", 9);

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: wrong JSON types are rejected, not coerced", "[serialization]")
{
    SECTION("gain as a string fails, not coerced to 0.0")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::String("abc"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("playing as a number fails, not coerced to true")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("playing", 1);

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("StateDelta gain as a string fails")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::String("abc"));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("StateDelta playing as a number fails")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("playing", 1);

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: non-finite numbers are rejected", "[serialization]")
{
    SECTION("positionSeconds = +infinity fails")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(std::numeric_limits<double>::infinity()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("positionSeconds = NaN fails")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(std::numeric_limits<double>::quiet_NaN()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("gain = -infinity fails")
    {
        auto v = djapp::toVar(makeValidState(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::var(-std::numeric_limits<double>::infinity()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: out-of-range numbers parse successfully, unclamped", "[serialization]")
{
    auto v = djapp::toVar(makeValidState(true));
    auto* obj = v.getDynamicObject();
    REQUIRE(obj != nullptr);
    obj->setProperty("gain", 5.0);

    auto result = djapp::fromVar<djapp::PlaybackState>(v);
    REQUIRE(isOk(result));
    auto parsed = unwrap(result);
    REQUIRE(parsed.gain == 5.0f);
}

TEST_CASE("Serialization: StateDelta.loop tri-state parses field-absent / null / object correctly", "[serialization]")
{
    SECTION("loop absent from JSON => outer nullopt")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->removeProperty("loop");

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);
        REQUIRE_FALSE(parsed.loop.has_value());
    }

    SECTION("loop explicitly null => outer has value, inner nullopt (clears loop)")
    {
        auto v = djapp::toVar(makeValidDelta(true));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("loop", juce::var());

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);
        REQUIRE(parsed.loop.has_value());
        REQUIRE_FALSE(parsed.loop->has_value());
    }

    SECTION("loop as an object => outer and inner both have values")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        auto* loopObj = new juce::DynamicObject();
        loopObj->setProperty("inSeconds", 3.0);
        loopObj->setProperty("outSeconds", 9.0);
        obj->setProperty("loop", juce::var(loopObj));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);
        REQUIRE(parsed.loop.has_value());
        REQUIRE(parsed.loop->has_value());
        REQUIRE((*parsed.loop)->inSeconds == 3.0);
        REQUIRE((*parsed.loop)->outSeconds == 9.0);
    }
}

TEST_CASE("Serialization: trackId validation", "[serialization]")
{
    SECTION("valid id string parses")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("Track-01.mp3_v2"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).trackId == "Track-01.mp3_v2");
    }

    SECTION("null trackId parses to empty juce::String")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::var());

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).trackId.isEmpty());
    }

    SECTION("trackId containing '/' fails")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("a/b"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("trackId containing '..' fails even though it matches the character class")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("a..b"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("trackId exactly '.' fails")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("."));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("trackId exactly '..' fails")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String(".."));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("trackId over 64 chars fails")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String::repeatedString("a", 65));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("trackId of exactly 64 chars parses")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String::repeatedString("a", 64));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
    }

    SECTION("literal empty string trackId fails")
    {
        auto v = djapp::toVar(makeValidState(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String());

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: StateDelta.deck is required and strictly validated", "[serialization]")
{
    SECTION("missing deck field fails")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->removeProperty("deck");

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("deck = 'Z' fails")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("deck", juce::String("Z"));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("deck = 'A' succeeds")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("deck", juce::String("A"));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).deck == djapp::DeckId::A);
    }

    SECTION("deck = 'B' succeeds")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("deck", juce::String("B"));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).deck == djapp::DeckId::B);
    }
}

TEST_CASE("Serialization: StateDelta.loop ordering is rejected", "[serialization]")
{
    SECTION("inSeconds > outSeconds fails")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        auto* loopObj = new juce::DynamicObject();
        loopObj->setProperty("inSeconds", 10.0);
        loopObj->setProperty("outSeconds", 5.0);
        obj->setProperty("loop", juce::var(loopObj));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("inSeconds == outSeconds fails (range requires strict <)")
    {
        auto v = djapp::toVar(makeValidDelta(false));
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        auto* loopObj = new juce::DynamicObject();
        loopObj->setProperty("inSeconds", 5.0);
        loopObj->setProperty("outSeconds", 5.0);
        obj->setProperty("loop", juce::var(loopObj));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }
}

TEST_CASE("Serialization: a failing parse reports failure and nothing more is asserted about its contents", "[serialization]")
{
    auto v = djapp::toVar(makeValidState(true));
    auto* obj = v.getDynamicObject();
    REQUIRE(obj != nullptr);
    obj->setProperty("gain", juce::String("abc"));

    auto result = djapp::fromVar<djapp::PlaybackState>(v);
    REQUIRE_FALSE(isOk(result));
    // Intentionally not inspecting the parsed struct further: on failure the
    // spec makes no promise about its contents, so none is asserted here.
}
