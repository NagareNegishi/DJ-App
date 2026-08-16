// White-box tests for the M7 wire-envelope additions to model/Serialization.cpp,
// written after reading the implementation. These target internal branches the
// fixture-driven SerializationEnvelopeTest.cpp (black-box, corpus-shaped) and the
// M6-era SerializationEdgeCasesTest.cpp (fromVar<T>/toVar internals) do not reach:
//
//   - buildDelta() on an empty StateDelta (deck only, no optional fields engaged):
//     the "changes" object must come out empty, not omitted or malformed.
//   - messageServerSeq()'s overflow guard at the exact int boundary
//     (d > numeric_limits<int>::max()): the fixture corpus only ever carries small
//     serverSeq values, so this branch has no fixture to exercise it.
//   - parseLoopValue()'s three failure branches parseDeltaMessage's fixture-driven
//     tests never hit: inSeconds == outSeconds (equal, not "in after out"), a loop
//     object missing one of its two required keys, and a loop object carrying an
//     unrecognized extra key alongside otherwise-valid inSeconds/outSeconds.
//   - serializeForSend()'s 4096-byte boundary exactly, not just "clearly oversized"
//     (the existing black-box test uses a payload far past the limit).
//   - messageType() when "type" is present, is a string, but is empty: the header
//     comment's "missing/not a string" contract does not mention this case, so it's
//     worth pinning what actually happens.

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "model/Serialization.h"
#include "model/Types.h"

using namespace djapp;

namespace
{
juce::var parseWireMessage(const juce::String& json)
{
    return juce::JSON::parse(json);
}
} // namespace

// ---------------------------------------------------------------------------------------
// buildDelta on an empty StateDelta
// ---------------------------------------------------------------------------------------

TEST_CASE("buildDelta on an empty StateDelta (deck only) produces an empty changes object",
          "[Serialization][envelope][edge][buildDelta]")
{
    StateDelta delta; // default-constructed: deck = A, no optional field engaged
    REQUIRE(delta.empty());

    auto msg = buildDelta(delta);
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 3); // type, deck, changes
    CHECK(msg.getProperty("type", juce::var()).toString() == "delta");
    CHECK(msg.getProperty("deck", juce::var()).toString() == "A");

    auto changes = msg.getProperty("changes", juce::var());
    auto* changesObj = changes.getDynamicObject();
    REQUIRE(changesObj != nullptr);
    CHECK(changesObj->getProperties().size() == 0);
}

// ---------------------------------------------------------------------------------------
// messageServerSeq overflow boundary
// ---------------------------------------------------------------------------------------

TEST_CASE("messageServerSeq accepts serverSeq exactly at INT_MAX", "[Serialization][envelope][edge][messageServerSeq]")
{
    const double maxAsDouble = static_cast<double>(std::numeric_limits<int>::max());
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "snapshot");
    obj->setProperty("serverSeq", maxAsDouble);
    juce::var msg(obj.get());

    auto seq = messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == std::numeric_limits<int>::max());
}

TEST_CASE("messageServerSeq rejects serverSeq one past INT_MAX (integer-valued, non-negative, but overflows int)",
          "[Serialization][envelope][edge][messageServerSeq]")
{
    const double pastMax = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "snapshot");
    obj->setProperty("serverSeq", pastMax);
    juce::var msg(obj.get());

    CHECK_FALSE(messageServerSeq(msg).has_value());
}

// ---------------------------------------------------------------------------------------
// parseDeltaMessage / parseLoopValue — branches the fixture corpus never exercises
// ---------------------------------------------------------------------------------------

TEST_CASE("parseDeltaMessage rejects a loop with inSeconds == outSeconds (equal, not just \"in after out\")",
          "[Serialization][envelope][edge][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","deck":"A","changes":{"loop":{"inSeconds":5.0,"outSeconds":5.0}}})");
    auto result = parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects a loop object missing outSeconds",
          "[Serialization][envelope][edge][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"A","changes":{"loop":{"inSeconds":5.0}}})");
    auto result = parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects a loop object missing inSeconds",
          "[Serialization][envelope][edge][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"A","changes":{"loop":{"outSeconds":5.0}}})");
    auto result = parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects a loop object carrying an unrecognized extra key",
          "[Serialization][envelope][edge][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","deck":"A",)"
        R"("changes":{"loop":{"inSeconds":1.0,"outSeconds":5.0,"bogus":true}}})");
    auto result = parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// serializeForSend exact byte boundary
// ---------------------------------------------------------------------------------------

TEST_CASE("serializeForSend succeeds for a message serialized to exactly 4096 bytes",
          "[Serialization][envelope][edge][serializeForSend]")
{
    // {"type":"hello","filler":"...."} — pad `filler` so the whole serialized
    // document is exactly 4096 bytes, then verify boundary inclusivity.
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "hello");
    obj->setProperty("filler", juce::String());
    juce::var probe(obj.get());
    const auto baseline = juce::JSON::toString(probe, true).getNumBytesAsUTF8();
    REQUIRE(baseline < 4096);

    const auto padLength = static_cast<int>(4096 - baseline);
    obj->setProperty("filler", juce::String::repeatedString("x", padLength));
    juce::var msg(obj.get());
    REQUIRE(juce::JSON::toString(msg, true).getNumBytesAsUTF8() == 4096);

    auto result = serializeForSend(msg);
    REQUIRE(result.ok);
    CHECK(result.value.getNumBytesAsUTF8() == 4096);
}

TEST_CASE("serializeForSend fails for a message serialized to exactly 4097 bytes (one past the limit)",
          "[Serialization][envelope][edge][serializeForSend]")
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "hello");
    obj->setProperty("filler", juce::String());
    juce::var probe(obj.get());
    const auto baseline = juce::JSON::toString(probe, true).getNumBytesAsUTF8();
    REQUIRE(baseline < 4097);

    const auto padLength = static_cast<int>(4097 - baseline);
    obj->setProperty("filler", juce::String::repeatedString("x", padLength));
    juce::var msg(obj.get());
    REQUIRE(juce::JSON::toString(msg, true).getNumBytesAsUTF8() == 4097);

    auto result = serializeForSend(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// messageType with an empty-but-present string type
// ---------------------------------------------------------------------------------------

TEST_CASE("messageType returns empty string when type is present but is an empty string",
          "[Serialization][envelope][edge][messageType]")
{
    // Not covered by the header comment's stated contract ("missing/not a string"):
    // type IS present and IS a string here, just empty. Pinning actual behavior:
    // this collapses to the same "" as a genuinely missing type, which is also what
    // WebSocketTransport::dispatch() relies on to fall through to its "unrecognized
    // message" branch.
    auto msg = parseWireMessage(R"({"type":""})");
    CHECK(messageType(msg).isEmpty());
}
