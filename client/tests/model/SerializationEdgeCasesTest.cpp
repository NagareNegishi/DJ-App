// White-box tests for model/Serialization.cpp, written after reading the
// implementation. These target internal helpers reachable only through the
// public API (toVar / fromVar<T> / DeckId free functions / StateDelta::empty),
// specifically:
//   - asFiniteNumber(): the isDouble()/isInt()/isInt64() type-tag union, and
//     its finiteness check at floating-point extremes.
//   - hasOnlyKnownKeys() / the "expected an object" branch: non-object
//     juce::var values (arrays, bare numbers) passed where an object is
//     required, both at top level and for the `loop` sub-object.
//   - isValidTrackId(): charset boundary characters not tried by the
//     black-box suite (space, control char, non-ASCII, single '-'/'_' as
//     the sole character).
//   - DeckId::fromString/toString, exercised both directly and through the
//     StateDelta.deck field's non-string-type branch.
//   - Result<T>'s failure invariant (ok == false, error non-empty) across
//     several distinct failure paths, which the black-box suite never
//     inspects (it only checks isOk()).
//   - StateDelta::empty(), a public Types.h method the black-box suite for
//     this unit never exercises.
//
// Nothing here duplicates a black-box case; every SECTION below targets a
// branch or boundary the spec-only suite has no way to know exists.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>

#include "model/Serialization.h"
#include "model/Types.h"

namespace
{

template <typename R> bool isOk(const R& r)
{
    return static_cast<bool>(r);
}

template <typename R> auto unwrap(const R& r)
{
    return *r;
}

djapp::PlaybackState makeValidState()
{
    djapp::PlaybackState s;
    s.trackId = "track_01";
    s.playing = true;
    s.positionSeconds = 123.456;
    s.gain = 0.75f;
    s.playbackRate = 1.25f;
    s.pitchOffsetSemitones = -3.5f;
    return s;
}

djapp::StateDelta makeValidDelta()
{
    djapp::StateDelta d;
    d.deck = djapp::DeckId::B;
    d.trackId = "abc";
    d.playing = true;
    d.positionSeconds = 42.0;
    d.gain = 1.5f;
    d.playbackRate = 0.8f;
    d.pitchOffsetSemitones = 2.0f;
    return d;
}

} // namespace

TEST_CASE("asFiniteNumber: integer-typed JSON numbers are accepted for double fields", "[serialization][edge]")
{
    SECTION("plain int accepted for positionSeconds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(42));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).positionSeconds == 42.0);
    }

    SECTION("int64 accepted for positionSeconds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(static_cast<juce::int64>(123456789012LL)));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).positionSeconds == 123456789012.0);
    }
}

TEST_CASE("asFiniteNumber: a JSON boolean is not accepted as a number", "[serialization][edge]")
{
    // isBool() is a distinct type tag from isDouble()/isInt()/isInt64() in
    // juce::var, so a bool value must be rejected exactly like a string,
    // even though it round-trips through some languages' truthy-numeric
    // coercions.
    auto v = djapp::toVar(makeValidState());
    auto* obj = v.getDynamicObject();
    REQUIRE(obj != nullptr);
    obj->setProperty("gain", juce::var(true));

    auto result = djapp::fromVar<djapp::PlaybackState>(v);
    REQUIRE_FALSE(isOk(result));
}

TEST_CASE("asFiniteNumber: floating-point extremes that are finite are accepted", "[serialization][edge]")
{
    SECTION("largest finite double accepted for positionSeconds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(std::numeric_limits<double>::max()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).positionSeconds == std::numeric_limits<double>::max());
    }

    SECTION("smallest positive subnormal double accepted for positionSeconds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(std::numeric_limits<double>::denorm_min()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).positionSeconds == std::numeric_limits<double>::denorm_min());
    }

    SECTION("negative zero accepted and its sign is preserved through the parse")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::var(-0.0));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        auto parsed = unwrap(result);
        REQUIRE(parsed.gain == 0.0f); // -0.0 == 0.0 under IEEE 754 comparison
        REQUIRE(std::signbit(parsed.gain));
    }
}

TEST_CASE("asFiniteFloat: a double that is finite but overflows float range is rejected", "[serialization][edge]")
{
    // gain/playbackRate/pitchOffsetSemitones are stored as float but validated
    // from a JSON number via a double finiteness check first; a value finite
    // as a double (e.g. 1e300) can narrow to +/-Infinity in the float, which
    // must be a parse failure rather than silently producing a non-finite
    // struct field.
    SECTION("gain = 1e300 (finite as double, +Infinity as float) is rejected")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::var(1e300));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("playbackRate = -1e300 (finite as double, -Infinity as float) is rejected")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("playbackRate", juce::var(-1e300));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("pitchOffsetSemitones = 1e300 is rejected through StateDelta too")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("pitchOffsetSemitones", juce::var(1e300));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("positionSeconds = 1e300 is unaffected (double field, not narrowed)")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(1e300));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).positionSeconds == 1e300);
    }
}

TEST_CASE("Non-object juce::var values are rejected cleanly at top level", "[serialization][edge]")
{
    SECTION("a JSON array is not an object")
    {
        juce::Array<juce::var> arr;
        arr.add(1);
        arr.add(2);
        auto result = djapp::fromVar<djapp::PlaybackState>(juce::var(arr));
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("a bare number is not an object")
    {
        auto result = djapp::fromVar<djapp::PlaybackState>(juce::var(3.14));
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("a bare string is not an object, for StateDelta too")
    {
        auto result = djapp::fromVar<djapp::StateDelta>(juce::var("not an object"));
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }
}

TEST_CASE("The loop field rejects non-object, non-null values cleanly (no crash)", "[serialization][edge]")
{
    SECTION("loop as a JSON array fails")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        juce::Array<juce::var> arr;
        arr.add(1.0);
        arr.add(2.0);
        obj->setProperty("loop", juce::var(arr));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("loop as a bare number fails")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("loop", juce::var(7));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }
}

TEST_CASE("trackId charset boundary characters", "[serialization][edge]")
{
    SECTION("a space fails")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String(" "));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("a control character (tab) fails")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String::charToString(static_cast<juce::juce_wchar>(9)));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("a non-ASCII unicode character fails")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId",
                         juce::String::charToString(static_cast<juce::juce_wchar>(0x00E9))); // 'e' with acute

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(isOk(result));
    }

    SECTION("a lone '-' (1 char, boundary charset member) succeeds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("-"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).trackId == "-");
    }

    SECTION("a lone '_' (1 char, boundary charset member) succeeds")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("_"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).trackId == "_");
    }

    SECTION("repeated '-' with no dots is not caught by the '..' path-traversal guard")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("--"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).trackId == "--");
    }
}

TEST_CASE("DeckId::fromString / toString direct round trip", "[serialization][edge]")
{
    REQUIRE(djapp::toString(djapp::DeckId::A) == "A");
    REQUIRE(djapp::toString(djapp::DeckId::B) == "B");

    REQUIRE(djapp::fromString("A") == djapp::DeckId::A);
    REQUIRE(djapp::fromString("B") == djapp::DeckId::B);

    SECTION("lowercase is rejected (case-sensitive)")
    {
        REQUIRE_FALSE(djapp::fromString("a").has_value());
    }

    SECTION("empty string is rejected")
    {
        REQUIRE_FALSE(djapp::fromString("").has_value());
    }

    SECTION("multi-character garbage is rejected")
    {
        REQUIRE_FALSE(djapp::fromString("AB").has_value());
    }
}

TEST_CASE("StateDelta.deck rejects a non-string JSON value cleanly", "[serialization][edge]")
{
    SECTION("deck as a number")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("deck", juce::var(1));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("deck as a boolean")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("deck", juce::var(true));

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(isOk(result));
        REQUIRE(result.error.isNotEmpty());
    }
}

TEST_CASE("Result<T> failure invariant: ok is false and error is non-empty across distinct failure paths",
          "[serialization][edge]")
{
    SECTION("missing required deck field")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->removeProperty("deck");

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("invalid trackId")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("trackId", juce::String("a/b"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("unknown top-level field")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("bogus", 1);

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("non-finite number")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("positionSeconds", juce::var(std::numeric_limits<double>::infinity()));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("wrong JSON type")
    {
        auto v = djapp::toVar(makeValidState());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->setProperty("gain", juce::String("abc"));

        auto result = djapp::fromVar<djapp::PlaybackState>(v);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.isNotEmpty());
    }

    SECTION("failure's default-constructed value is the type's default, not left indeterminate")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        obj->removeProperty("deck");

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE_FALSE(result.ok);
        // Result<T>'s documented contract: value is default-constructed T{} on failure.
        REQUIRE(result.value.deck == djapp::DeckId::A); // StateDelta{}'s default
        REQUIRE_FALSE(result.value.trackId.has_value());
    }
}

TEST_CASE("StateDelta::empty() reflects exactly which optional fields are engaged", "[serialization][edge]")
{
    SECTION("default-constructed delta is empty")
    {
        djapp::StateDelta d;
        REQUIRE(d.empty());
    }

    SECTION("setting any single scalar field makes it non-empty")
    {
        djapp::StateDelta d;
        d.gain = 1.0f;
        REQUIRE_FALSE(d.empty());
    }

    SECTION("loop engaged as an explicit clear (outer has_value, inner nullopt) counts as non-empty")
    {
        djapp::StateDelta d;
        d.loop = std::optional<djapp::LoopPoints>(std::nullopt);
        REQUIRE_FALSE(d.empty());
    }

    SECTION("a delta parsed from a minimal {deck} JSON object is empty")
    {
        auto v = djapp::toVar(makeValidDelta());
        auto* obj = v.getDynamicObject();
        REQUIRE(obj != nullptr);
        // Strip everything but the required "deck" field.
        obj->removeProperty("trackId");
        obj->removeProperty("playing");
        obj->removeProperty("positionSeconds");
        obj->removeProperty("gain");
        obj->removeProperty("playbackRate");
        obj->removeProperty("pitchOffsetSemitones");

        auto result = djapp::fromVar<djapp::StateDelta>(v);
        REQUIRE(isOk(result));
        REQUIRE(unwrap(result).empty());
    }
}
