// Black-box coverage for the wire-envelope functions added to model/Serialization.h
// (Unit T, "File 1"): the build* envelope constructors, messageType, parseDeltaMessage,
// messageServerSeq, describeCloseCode, and serializeForSend. Does not touch
// sync/WebSocketTransport (out of scope for this file) or PlaybackState (de)serialization
// (covered elsewhere).
//
// Expected wire shapes are copied verbatim from shared/protocol/fixtures/{client-to-server,
// server-to-client}/{valid,invalid}/*.json (read directly against the staged manifest, not
// guessed). A couple of parseDeltaMessage's narrow-flatten failure conditions have no
// matching fixture in the corpus and are hand-built instead — each is called out at its
// call site below.

#include "model/Serialization.h"
#include "model/Types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <optional>
#include <set>

namespace
{
juce::var parseWireMessage(const char* json)
{
    return juce::JSON::parse(juce::String(json));
}
} // namespace

// ---------------------------------------------------------------------------------------
// buildHello
// ---------------------------------------------------------------------------------------

TEST_CASE("buildHello accepts valid name/room and matches hello-minimal.json", "[Serialization][envelope][buildHello]")
{
    auto result = djapp::buildHello("nagare", "demo-room");
    REQUIRE(result.ok);
    auto& msg = result.value;
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 4); // type, protocolVersion, name, room
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "hello");
    REQUIRE((int)msg.getProperty("protocolVersion", juce::var()) == djapp::kProtocolVersion);
    REQUIRE(msg.getProperty("name", juce::var()).toString() == "nagare");
    REQUIRE(msg.getProperty("room", juce::var()).toString() == "demo-room");
}

TEST_CASE("buildHello accepts a name at the 32-character maximum, matching hello-name-max-length.json",
          "[Serialization][envelope][buildHello]")
{
    const juce::String maxName = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; // exactly 32 chars, per fixture
    REQUIRE(maxName.length() == 32);
    auto result = djapp::buildHello(maxName, "demo-room");
    REQUIRE(result.ok);
    REQUIRE(result.value.getProperty("name", juce::var()).toString() == maxName);
}

TEST_CASE("buildHello accepts a room at the 64-character maximum, matching hello-room-max-length.json",
          "[Serialization][envelope][buildHello]")
{
    // Copied verbatim from the fixture's "room" value.
    const juce::String maxRoom = "demo-room-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    REQUIRE(maxRoom.length() == 64);
    auto result = djapp::buildHello("nagare", maxRoom);
    REQUIRE(result.ok);
    REQUIRE(result.value.getProperty("room", juce::var()).toString() == maxRoom);
}

TEST_CASE("buildHello rejects an empty name", "[Serialization][envelope][buildHello]")
{
    auto result = djapp::buildHello("", "demo-room");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("buildHello rejects a name over 32 characters", "[Serialization][envelope][buildHello]")
{
    const juce::String maxName = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; // 32 chars, still valid
    REQUIRE(maxName.length() == 32);                                 // guards the boundary derived below
    const juce::String tooLong = maxName + "a";                      // 33 chars
    auto result = djapp::buildHello(tooLong, "demo-room");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("buildHello rejects a name containing a control character", "[Serialization][envelope][buildHello]")
{
    // "\x01" is a single non-printable control byte; the rest of the literal is plain text,
    // so this isolates the control-character rule from the length rule (10 chars total).
    const juce::String controlName = "valid\x01name";
    auto result = djapp::buildHello(controlName, "demo-room");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("buildHello rejects an empty room", "[Serialization][envelope][buildHello]")
{
    auto result = djapp::buildHello("nagare", "");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("buildHello rejects a room over 64 characters", "[Serialization][envelope][buildHello]")
{
    const juce::String maxRoom = "demo-room-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; // 64 chars
    REQUIRE(maxRoom.length() == 64);            // guards the boundary derived below
    const juce::String tooLong = maxRoom + "a"; // 65 chars
    auto result = djapp::buildHello("nagare", tooLong);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// buildClaimControl / buildReleaseControl / buildRequestSnapshot
// ---------------------------------------------------------------------------------------

TEST_CASE("buildClaimControl matches claim-control-bare.json", "[Serialization][envelope]")
{
    auto msg = djapp::buildClaimControl();
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 1);
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "claimControl");
}

TEST_CASE("buildReleaseControl matches release-control-bare.json", "[Serialization][envelope]")
{
    auto msg = djapp::buildReleaseControl();
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 1);
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "releaseControl");
}

TEST_CASE("buildRequestSnapshot matches request-snapshot-bare.json", "[Serialization][envelope]")
{
    auto msg = djapp::buildRequestSnapshot();
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 1);
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "requestSnapshot");
}

// ---------------------------------------------------------------------------------------
// buildDelta
// ---------------------------------------------------------------------------------------

TEST_CASE("buildDelta with a single changed field matches delta-deck-b.json", "[Serialization][envelope][buildDelta]")
{
    djapp::StateDelta delta;
    delta.deck = djapp::DeckId::B;
    delta.gain = 0.8f;

    auto msg = djapp::buildDelta(delta);
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 3); // type, deck, changes
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "delta");
    REQUIRE(msg.getProperty("deck", juce::var()).toString() == "B");

    auto changes = msg.getProperty("changes", juce::var());
    auto* changesObj = changes.getDynamicObject();
    REQUIRE(changesObj != nullptr);
    REQUIRE(changesObj->getProperties().size() == 1);
    REQUIRE((double)changes.getProperty("gain", juce::var()) == Catch::Approx(0.8));
}

TEST_CASE("buildDelta with every field set matches delta-all-fields.json", "[Serialization][envelope][buildDelta]")
{
    djapp::StateDelta delta;
    delta.deck = djapp::DeckId::A;
    delta.trackId = juce::String("demo-track-02");
    delta.playing = true;
    delta.positionSeconds = 30.25;
    delta.gain = 1.25f;
    delta.playbackRate = 1.5f;
    delta.pitchOffsetSemitones = -3.0f;
    delta.loop = std::optional<djapp::LoopPoints>(djapp::LoopPoints{10.0, 20.0});

    auto msg = djapp::buildDelta(delta);
    auto* obj = msg.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(obj->getProperties().size() == 3);
    REQUIRE(msg.getProperty("type", juce::var()).toString() == "delta");
    REQUIRE(msg.getProperty("deck", juce::var()).toString() == "A");

    auto changes = msg.getProperty("changes", juce::var());
    auto* changesObj = changes.getDynamicObject();
    REQUIRE(changesObj != nullptr);
    REQUIRE(changesObj->getProperties().size() == 7);
    REQUIRE(changes.getProperty("trackId", juce::var()).toString() == "demo-track-02");
    REQUIRE((bool)changes.getProperty("playing", juce::var()) == true);
    REQUIRE((double)changes.getProperty("positionSeconds", juce::var()) == Catch::Approx(30.25));
    REQUIRE((double)changes.getProperty("gain", juce::var()) == Catch::Approx(1.25));
    REQUIRE((double)changes.getProperty("playbackRate", juce::var()) == Catch::Approx(1.5));
    REQUIRE((double)changes.getProperty("pitchOffsetSemitones", juce::var()) == Catch::Approx(-3.0));

    auto loopVar = changes.getProperty("loop", juce::var());
    auto* loopObj = loopVar.getDynamicObject();
    REQUIRE(loopObj != nullptr);
    REQUIRE((double)loopVar.getProperty("inSeconds", juce::var()) == Catch::Approx(10.0));
    REQUIRE((double)loopVar.getProperty("outSeconds", juce::var()) == Catch::Approx(20.0));
}

// ---------------------------------------------------------------------------------------
// messageType
// ---------------------------------------------------------------------------------------

TEST_CASE("messageType reads the type field of known message shapes", "[Serialization][envelope][messageType]")
{
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"hello"})")) == "hello");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"delta"})")) == "delta");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"welcome"})")) == "welcome");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"snapshot"})")) == "snapshot");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"roleChanged"})")) == "roleChanged");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"peerJoined"})")) == "peerJoined");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"peerLeft"})")) == "peerLeft");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"error"})")) == "error");
    CHECK(djapp::messageType(parseWireMessage(R"({"type":"claimControl"})")) == "claimControl");
}

TEST_CASE("messageType returns empty string when type is missing", "[Serialization][envelope][messageType]")
{
    REQUIRE(djapp::messageType(parseWireMessage(R"({"clientId":"c-1"})")).isEmpty());
}

TEST_CASE("messageType returns empty string when type is not a string", "[Serialization][envelope][messageType]")
{
    REQUIRE(djapp::messageType(parseWireMessage(R"({"type":5})")).isEmpty());
}

TEST_CASE("messageType returns empty string for a non-object message (array)", "[Serialization][envelope][messageType]")
{
    REQUIRE(djapp::messageType(parseWireMessage(R"([1,2,3])")).isEmpty());
}

TEST_CASE("messageType returns empty string for a non-object message (bare number)",
          "[Serialization][envelope][messageType]")
{
    REQUIRE(djapp::messageType(juce::var(42)).isEmpty());
}

// ---------------------------------------------------------------------------------------
// parseDeltaMessage — accept paths, driven by server-to-client/valid delta fixtures
// ---------------------------------------------------------------------------------------

TEST_CASE("parseDeltaMessage parses delta-broadcast.json", "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":7,"sourceClientId":"c-3f2a","deck":"A","changes":{"gain":1.5}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE(result.ok);
    CHECK(result.value.deck == djapp::DeckId::A);
    REQUIRE(result.value.gain.has_value());
    CHECK(*result.value.gain == Catch::Approx(1.5));
    CHECK_FALSE(result.value.trackId.has_value());
    CHECK_FALSE(result.value.playing.has_value());
    CHECK_FALSE(result.value.positionSeconds.has_value());
    CHECK_FALSE(result.value.playbackRate.has_value());
    CHECK_FALSE(result.value.pitchOffsetSemitones.has_value());
    CHECK_FALSE(result.value.loop.has_value());
}

TEST_CASE("parseDeltaMessage parses delta-broadcast-all-fields.json", "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","serverSeq":8,"sourceClientId":"c-9b01","deck":"B","changes":{)"
                                R"("trackId":"demo-track-02","playing":true,"positionSeconds":30.25,"gain":1.25,)"
                                R"("playbackRate":1.5,"pitchOffsetSemitones":-3,)"
                                R"("loop":{"inSeconds":10.0,"outSeconds":20.0}}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE(result.ok);
    CHECK(result.value.deck == djapp::DeckId::B);
    REQUIRE(result.value.trackId.has_value());
    CHECK(*result.value.trackId == "demo-track-02");
    REQUIRE(result.value.playing.has_value());
    CHECK(*result.value.playing == true);
    REQUIRE(result.value.positionSeconds.has_value());
    CHECK(*result.value.positionSeconds == Catch::Approx(30.25));
    REQUIRE(result.value.gain.has_value());
    CHECK(*result.value.gain == Catch::Approx(1.25));
    REQUIRE(result.value.playbackRate.has_value());
    CHECK(*result.value.playbackRate == Catch::Approx(1.5));
    REQUIRE(result.value.pitchOffsetSemitones.has_value());
    CHECK(*result.value.pitchOffsetSemitones == Catch::Approx(-3.0));
    REQUIRE(result.value.loop.has_value());
    REQUIRE(result.value.loop->has_value());
    CHECK(result.value.loop->value().inSeconds == Catch::Approx(10.0));
    CHECK(result.value.loop->value().outSeconds == Catch::Approx(20.0));
}

TEST_CASE("parseDeltaMessage accepts extra top-level envelope fields (narrow flatten reads only deck/changes)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    // serverSeq and sourceClientId are top-level fields the flatten step never reads;
    // their presence must not cause a parse failure (spec's "narrow flatten" guarantee).
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":99,"sourceClientId":"c-anything","deck":"A","changes":{"gain":1.0}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE(result.ok);
    CHECK(result.value.deck == djapp::DeckId::A);
}

// ---------------------------------------------------------------------------------------
// parseDeltaMessage — hand-built reject paths
//
// The staged server-to-client/invalid/ corpus (per fixtures/MANIFEST.txt) has no fixture
// covering "deck absent" or "changes not an object" — these are spec-level guarantees
// (spec.md: "Fails if deck is absent or changes is not an object") that the shared corpus
// doesn't happen to exercise. Hand-built here for exactly that reason; not fixture-sourced.
// ---------------------------------------------------------------------------------------

TEST_CASE("parseDeltaMessage rejects a message with no deck field (hand-built, no matching fixture)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","changes":{"gain":1.0}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE(
    "parseDeltaMessage rejects a message whose changes is an array, not an object (hand-built, no matching fixture)",
    "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"A","changes":[1,2,3]})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE(
    "parseDeltaMessage rejects a message whose changes is a string, not an object (hand-built, no matching fixture)",
    "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"A","changes":"nope"})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects a message with no changes field at all (hand-built, no matching fixture)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"A"})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects an invalid deck value (hand-built, no matching fixture)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","deck":"C","changes":{"gain":1.0}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// parseDeltaMessage — driven by server-to-client/invalid/*.json's six delta-* fixtures.
// Each fixture declares expect.client; per spec.md's "narrow flatten" paragraph, this is
// NOT uniformly "reject" — out-of-range-but-well-typed values are accepted by parse and
// left for the caller's later ranges::clamp() (Types.h), so gain-above-max is "accept"
// even though the fixture directory is named "invalid" (invalid per server-side rules,
// not necessarily per the client's own parse contract).
// ---------------------------------------------------------------------------------------

TEST_CASE(
    "parseDeltaMessage accepts delta-gain-above-max.json (expect.client: accept — range is clamp's job, not parse's)",
    "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":17,"sourceClientId":"c-9b01","deck":"A","changes":{"gain":5.0}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE(result.ok);
    CHECK(result.value.deck == djapp::DeckId::A);
    REQUIRE(result.value.gain.has_value());
    CHECK(*result.value.gain == Catch::Approx(5.0)); // unclamped: clamping is not this function's job
}

TEST_CASE("parseDeltaMessage rejects delta-gain-not-a-number.json (expect.client: reject — gain sent as a string)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":14,"sourceClientId":"c-9b01","deck":"A","changes":{"gain":"Infinity"}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects delta-loop-in-after-out.json (expect.client: reject — inSeconds not < outSeconds)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","serverSeq":13,"sourceClientId":"c-9b01","deck":"A",)"
                                R"("changes":{"loop":{"inSeconds":8.0,"outSeconds":4.0}}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects delta-playing-string.json (expect.client: reject — playing sent as a string)",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(R"({"type":"delta","serverSeq":10,"sourceClientId":"c-9b01","deck":"A",)"
                                R"("changes":{"playing":"true","positionSeconds":5}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE("parseDeltaMessage rejects delta-track-id-dot-dot.json (expect.client: reject — trackId contains \"..\")",
          "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":11,"sourceClientId":"c-9b01","deck":"A","changes":{"trackId":"demo..track"}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

TEST_CASE(
    "parseDeltaMessage rejects delta-unknown-field-in-changes.json (expect.client: reject — \"volume\" is undefined)",
    "[Serialization][envelope][parseDeltaMessage]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":9,"sourceClientId":"c-9b01","deck":"A","changes":{"volume":0.5}})");
    auto result = djapp::parseDeltaMessage(msg);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// messageServerSeq
// ---------------------------------------------------------------------------------------

TEST_CASE("messageServerSeq reads serverSeq off delta-broadcast.json (7)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":7,"sourceClientId":"c-3f2a","deck":"A","changes":{"gain":1.5}})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 7);
}

TEST_CASE("messageServerSeq reads serverSeq off delta-broadcast-all-fields.json (8)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(
        R"({"type":"delta","serverSeq":8,"sourceClientId":"c-9b01","deck":"B","changes":{"gain":1.0}})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 8);
}

TEST_CASE("messageServerSeq reads serverSeq off snapshot-both-decks.json (31)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":31,"decks":{}})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 31);
}

TEST_CASE("messageServerSeq reads serverSeq off snapshot-deck-a-only.json (12)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":12,"decks":{}})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 12);
}

TEST_CASE("messageServerSeq reads serverSeq off welcome-first-join.json (0)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"welcome","clientId":"c-3f2a","role":"observer","serverSeq":0,)"
                                R"("snapshot":{"decks":{"A":{"trackId":null,"playing":false,"positionSeconds":0,)"
                                R"("gain":1.0,"playbackRate":1.0,"pitchOffsetSemitones":0,"loop":null}}},)"
                                R"("peers":[],"source":"https://github.com/NagareNegishi/DJ-App"})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 0);
}

TEST_CASE("messageServerSeq reads serverSeq off welcome-populated-room.json (42)",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"welcome","clientId":"c-3f2a","role":"observer","serverSeq":42,)"
                                R"("snapshot":{"decks":{"A":{"trackId":null,"playing":false,"positionSeconds":0,)"
                                R"("gain":1.0,"playbackRate":1.0,"pitchOffsetSemitones":0,"loop":null}}},)"
                                R"("peers":[{"clientId":"c-9b01","name":"aki","role":"controller"},)"
                                R"({"clientId":"c-71cd","name":"mika","role":"observer"}],)"
                                R"("source":"https://github.com/NagareNegishi/DJ-App"})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 42);
}

TEST_CASE("messageServerSeq is nullopt when serverSeq is absent", "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"roleChanged","clientId":"c-1","role":"observer"})");
    CHECK_FALSE(djapp::messageServerSeq(msg).has_value());
}

TEST_CASE("messageServerSeq is nullopt when serverSeq is a string", "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":"7","decks":{}})");
    CHECK_FALSE(djapp::messageServerSeq(msg).has_value());
}

TEST_CASE("messageServerSeq is nullopt when serverSeq is negative", "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":-1,"decks":{}})");
    CHECK_FALSE(djapp::messageServerSeq(msg).has_value());
}

TEST_CASE("messageServerSeq is nullopt when serverSeq is a non-integer-valued number",
          "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":3.5,"decks":{}})");
    CHECK_FALSE(djapp::messageServerSeq(msg).has_value());
}

TEST_CASE("messageServerSeq accepts an integer-valued double", "[Serialization][envelope][messageServerSeq]")
{
    auto msg = parseWireMessage(R"({"type":"snapshot","serverSeq":5.0,"decks":{}})");
    auto seq = djapp::messageServerSeq(msg);
    REQUIRE(seq.has_value());
    CHECK(*seq == 5);
}

TEST_CASE("messageServerSeq is nullopt for a non-object message", "[Serialization][envelope][messageServerSeq]")
{
    CHECK_FALSE(djapp::messageServerSeq(parseWireMessage(R"([1,2,3])")).has_value());
}

// ---------------------------------------------------------------------------------------
// describeCloseCode
// ---------------------------------------------------------------------------------------

TEST_CASE("describeCloseCode returns a distinct, non-empty string for every documented close code",
          "[Serialization][envelope][describeCloseCode]")
{
    const int documentedCodes[] = {4000, 4001, 4002, 4003, 4004, 1008, 1009, 1001, 1011};
    std::set<juce::String> descriptions;
    for (int code : documentedCodes)
    {
        auto description = djapp::describeCloseCode(code);
        INFO("code = " << code);
        REQUIRE(description.isNotEmpty());
        descriptions.insert(description);
    }
    // Every documented code has "one fixed, complete meaning" (spec) — descriptions
    // must not collide, or a client-visible reason string would be ambiguous.
    CHECK(descriptions.size() == sizeof(documentedCodes) / sizeof(documentedCodes[0]));
}

TEST_CASE("describeCloseCode returns a safe non-empty fallback for an undocumented code",
          "[Serialization][envelope][describeCloseCode]")
{
    REQUIRE_NOTHROW(djapp::describeCloseCode(9999));
    CHECK(djapp::describeCloseCode(9999).isNotEmpty());
}

// ---------------------------------------------------------------------------------------
// serializeForSend
// ---------------------------------------------------------------------------------------

TEST_CASE("serializeForSend succeeds for a small message and round-trips via JSON::parse",
          "[Serialization][envelope][serializeForSend]")
{
    auto msg = djapp::buildRequestSnapshot();
    auto result = djapp::serializeForSend(msg);
    REQUIRE(result.ok);
    REQUIRE(result.value.getNumBytesAsUTF8() < 4096);

    auto roundTripped = juce::JSON::parse(result.value);
    REQUIRE(roundTripped.getDynamicObject() != nullptr);
    CHECK(roundTripped.getProperty("type", juce::var()).toString() == "requestSnapshot");
}

TEST_CASE("serializeForSend fails for a message whose serialized form exceeds 4096 bytes",
          "[Serialization][envelope][serializeForSend]")
{
    // buildDelta can't produce an oversize-but-otherwise-valid message (trackId is
    // regex/length constrained), so this constructs a large payload directly via
    // DynamicObject, per the spec's suggested fallback.
    auto* filler = new juce::DynamicObject();
    filler->setProperty("type", "hello");
    filler->setProperty("filler", juce::String::repeatedString("x", 4096));
    juce::var oversized(filler);

    auto result = djapp::serializeForSend(oversized);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.isNotEmpty());
    // Result<T>'s contract: value is unused/default-constructed on failure.
    CHECK(result.value.isEmpty());
}
