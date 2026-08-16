// Runs the shared wire-contract corpus in shared/protocol/fixtures through the client's
// model/Serialization parser. The corpus is derived from docs/plan/02-protocol.md and is
// shared with the Node server suite — it is not client-owned test data, so a fixture that
// disagrees with the parser is a drift report about one of the two, not a test to relax.
//
// Only server-to-client/ is run here: client-to-server/ is a server-only corpus (the
// client never parses its own outgoing messages and has no hello parser at M6).

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

#include "model/Serialization.h"
#include "model/Types.h"

#ifndef DJ_FIXTURE_DIR
#error "DJ_FIXTURE_DIR must be supplied by the build; this suite must not guess a relative fixture path"
#endif

namespace
{

struct FixtureEntry
{
    juce::File file;
    juce::String directoryName;

    std::string label() const { return (directoryName + "/" + file.getFileName()).toStdString(); }
};

bool byFileName(const juce::File& a, const juce::File& b)
{
    return a.getFileName().compare(b.getFileName()) < 0;
}

std::vector<juce::File> jsonFilesIn(const juce::File& directory)
{
    std::vector<juce::File> files;

    // A missing directory yields an empty list rather than iterating; the callers assert a
    // non-zero count, so an absent corpus fails loudly instead of passing on nothing.
    if (!directory.isDirectory())
        return files;

    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.json"))
        files.push_back(file);

    // Stable order so a failing section name is reproducible from one run to the next.
    std::sort(files.begin(), files.end(), byFileName);

    return files;
}

juce::File serverToClientDir(const juce::String& directoryName)
{
    return juce::File(DJ_FIXTURE_DIR).getChildFile("server-to-client").getChildFile(directoryName);
}

std::vector<FixtureEntry> fixturesIn(const juce::String& directoryName)
{
    std::vector<FixtureEntry> entries;

    for (const auto& file : jsonFilesIn(serverToClientDir(directoryName)))
        entries.push_back({file, directoryName});

    return entries;
}

std::vector<FixtureEntry> allServerToClientFixtures()
{
    auto entries = fixturesIn("valid");

    for (const auto& entry : fixturesIn("invalid"))
        entries.push_back(entry);

    return entries;
}

juce::StringArray splitPointerTokens(const juce::String& pointer)
{
    juce::StringArray tokens;
    const auto length = pointer.length();
    int start = 1; // the leading '/' introduces the first token, it does not separate two

    while (start <= length)
    {
        auto end = pointer.indexOfChar(start, '/');

        if (end < 0)
            end = length;

        tokens.add(pointer.substring(start, end));
        start = end + 1;
    }

    return tokens;
}

// RFC 6901. nullopt means the pointer does not resolve, which the corpus's contract makes a
// broken fixture. No fixture uses the ~0/~1 escapes, but a general walker must unescape them
// or it would silently resolve the wrong key the day one is added.
std::optional<juce::var> resolvePointer(const juce::var& document, const juce::String& pointer)
{
    if (pointer.isEmpty())
        return document;

    if (!pointer.startsWithChar('/'))
        return std::nullopt;

    juce::var current = document;

    for (const auto& token : splitPointerTokens(pointer))
    {
        const auto key = token.replace("~1", "/").replace("~0", "~");

        if (auto* object = current.getDynamicObject())
        {
            const auto& properties = object->getProperties();
            int found = -1;

            // Matched on the member's name rather than through juce::Identifier so a key the
            // corpus may carry but JUCE would not accept as an identifier resolves to "no such
            // member" instead of tripping an assertion.
            for (int i = 0; i < properties.size(); ++i)
                if (properties.getName(i).toString() == key)
                    found = i;

            if (found < 0)
                return std::nullopt;

            current = properties.getValueAt(found);
            continue;
        }

        if (auto* array = current.getArray())
        {
            if (key.isEmpty() || !key.containsOnly("0123456789"))
                return std::nullopt;

            const auto index = key.getIntValue();

            if (index >= array->size())
                return std::nullopt;

            current = array->getUnchecked(index);
            continue;
        }

        return std::nullopt;
    }

    return current;
}

struct ParseOutcome
{
    bool parsed = false;
    juce::String error;
};

// `as` is asserted to be one of the two supported values before this is reached.
ParseOutcome parsePayload(const juce::var& target, const std::string& as)
{
    if (as == "PlaybackState")
    {
        const auto result = djapp::fromVar<djapp::PlaybackState>(target);
        return {result.ok, result.error};
    }

    const auto result = djapp::parseDeltaMessage(target);
    return {result.ok, result.error};
}

juce::var loadFixture(const juce::File& file)
{
    juce::var root;
    const auto parseResult = juce::JSON::parse(file.loadFileAsString(), root);

    INFO("JSON parse error: " << parseResult.getErrorMessage().toStdString());
    REQUIRE(parseResult.wasOk());
    REQUIRE(root.getDynamicObject() != nullptr);

    return root;
}

juce::var requireMessage(const juce::var& root)
{
    auto* object = root.getDynamicObject();
    REQUIRE(object != nullptr);

    // Presence, not truthiness: a fixture may deliberately carry null, a number or an array.
    REQUIRE(object->hasProperty("message"));

    return object->getProperty("message");
}

juce::Array<juce::var> requirePayloads(const juce::var& root)
{
    auto* object = root.getDynamicObject();
    REQUIRE(object != nullptr);
    REQUIRE(object->hasProperty("payloads"));

    const auto payloads = object->getProperty("payloads");
    REQUIRE(payloads.isArray());

    return *payloads.getArray();
}

juce::String payloadPointer(const juce::var& payload)
{
    auto* object = payload.getDynamicObject();
    REQUIRE(object != nullptr);
    REQUIRE(object->hasProperty("pointer"));

    const auto pointer = object->getProperty("pointer");
    REQUIRE(pointer.isString());

    return pointer.toString();
}

std::string payloadAs(const juce::var& payload)
{
    auto* object = payload.getDynamicObject();
    REQUIRE(object != nullptr);
    REQUIRE(object->hasProperty("as"));

    const auto as = object->getProperty("as");
    REQUIRE(as.isString());

    return as.toString().toStdString();
}

// expect.client is required on every server-to-client/invalid fixture and has no default, so a
// fixture that omits it fails here instead of silently asserting the wrong rule.
std::string requireExpectClient(const juce::var& root)
{
    auto* object = root.getDynamicObject();
    REQUIRE(object != nullptr);
    REQUIRE(object->hasProperty("expect"));

    const auto expect = object->getProperty("expect");
    auto* expectObject = expect.getDynamicObject();
    REQUIRE(expectObject != nullptr);
    REQUIRE(expectObject->hasProperty("client"));

    const auto client = expectObject->getProperty("client");
    REQUIRE(client.isString());

    const auto value = client.toString().toStdString();
    INFO("expect.client: " << value);
    REQUIRE((value == "reject" || value == "accept"));

    return value;
}

} // namespace

TEST_CASE("Protocol fixtures: the server-to-client corpus directories exist and contain fixtures",
          "[protocol][fixtures]")
{
    SECTION("valid/ is present and non-empty")
    {
        const auto directory = serverToClientDir("valid");
        INFO("fixture directory: " << directory.getFullPathName().toStdString());
        REQUIRE(directory.isDirectory());
        REQUIRE_FALSE(jsonFilesIn(directory).empty());
    }

    SECTION("invalid/ is present and non-empty")
    {
        const auto directory = serverToClientDir("invalid");
        INFO("fixture directory: " << directory.getFullPathName().toStdString());
        REQUIRE(directory.isDirectory());
        REQUIRE_FALSE(jsonFilesIn(directory).empty());
    }
}

TEST_CASE("Protocol fixtures: every fixture is well-formed JSON carrying description, message and payloads",
          "[protocol][fixtures]")
{
    const auto fixtures = allServerToClientFixtures();
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);
            auto* object = root.getDynamicObject();

            REQUIRE(object->hasProperty("description"));
            REQUIRE(object->getProperty("description").isString());
            REQUIRE(object->getProperty("description").toString().isNotEmpty());

            requireMessage(root);
            requirePayloads(root);
        }
    }
}

TEST_CASE("Protocol fixtures: every payload entry declares a string pointer and a supported as", "[protocol][fixtures]")
{
    const auto fixtures = allServerToClientFixtures();
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);
            const auto payloads = requirePayloads(root);

            for (const auto& payload : payloads)
            {
                payloadPointer(payload);

                const auto as = payloadAs(payload);
                INFO("as: " << as);
                REQUIRE((as == "PlaybackState" || as == "StateDelta"));
            }
        }
    }
}

TEST_CASE("Protocol fixtures: every payload pointer resolves against its fixture's message", "[protocol][fixtures]")
{
    const auto fixtures = allServerToClientFixtures();
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);
            const auto message = requireMessage(root);

            for (const auto& payload : requirePayloads(root))
            {
                const auto pointer = payloadPointer(payload);
                INFO("pointer: '" << pointer.toStdString() << "'");
                REQUIRE(resolvePointer(message, pointer).has_value());
            }
        }
    }
}

TEST_CASE("Protocol fixtures: the corpus declares payloads to parse and payload-free messages to count",
          "[protocol][fixtures]")
{
    int declaredPayloads = 0;
    int payloadFreeFixtures = 0;

    for (const auto& entry : allServerToClientFixtures())
    {
        INFO(entry.label());
        const auto payloads = requirePayloads(loadFixture(entry.file));
        declaredPayloads += payloads.size();

        if (payloads.isEmpty())
            ++payloadFreeFixtures;
    }

    // Without these the parsing test cases below could pass while exercising the parser zero times.
    REQUIRE(declaredPayloads > 0);
    REQUIRE(payloadFreeFixtures > 0);
}

TEST_CASE("Protocol fixtures: every server-to-client/invalid fixture declares reason and expect.client",
          "[protocol][fixtures]")
{
    const auto fixtures = fixturesIn("invalid");
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);
            auto* object = root.getDynamicObject();

            REQUIRE(object->hasProperty("reason"));
            REQUIRE(object->getProperty("reason").isString());
            REQUIRE(object->getProperty("reason").toString().isNotEmpty());

            requireExpectClient(root);
        }
    }
}

TEST_CASE("Protocol fixtures: server-to-client/invalid covers both the reject and accept outcomes",
          "[protocol][fixtures]")
{
    int rejecting = 0;
    int accepting = 0;

    for (const auto& entry : fixturesIn("invalid"))
    {
        INFO(entry.label());

        if (requireExpectClient(loadFixture(entry.file)) == "reject")
            ++rejecting;
        else
            ++accepting;
    }

    // The two outcomes encode opposite halves of the validation policy: the server refuses an
    // out-of-range value while the client parses it and clamps later. Losing either half here
    // would leave one of the two rules untested.
    REQUIRE(rejecting > 0);
    REQUIRE(accepting > 0);
}

TEST_CASE("Protocol fixtures: every server-to-client/valid payload parses", "[protocol][fixtures]")
{
    const auto fixtures = fixturesIn("valid");
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);
            const auto message = requireMessage(root);

            for (const auto& payload : requirePayloads(root))
            {
                const auto pointer = payloadPointer(payload);
                const auto as = payloadAs(payload);
                INFO("pointer: '" << pointer.toStdString() << "' as: " << as);

                const auto target = resolvePointer(message, pointer);
                REQUIRE(target.has_value());

                const auto outcome = parsePayload(*target, as);
                INFO("parse error: " << outcome.error.toStdString());
                REQUIRE(outcome.parsed);
            }
        }
    }
}

TEST_CASE("Protocol fixtures: every payload of an expect.client reject fixture fails to parse", "[protocol][fixtures]")
{
    const auto fixtures = fixturesIn("invalid");
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);

            if (requireExpectClient(root) == "reject")
            {
                const auto message = requireMessage(root);

                for (const auto& payload : requirePayloads(root))
                {
                    const auto pointer = payloadPointer(payload);
                    const auto as = payloadAs(payload);
                    INFO("pointer: '" << pointer.toStdString() << "' as: " << as);

                    const auto target = resolvePointer(message, pointer);
                    REQUIRE(target.has_value());

                    REQUIRE_FALSE(parsePayload(*target, as).parsed);
                }
            }
        }
    }
}

TEST_CASE("Protocol fixtures: every payload of an expect.client accept fixture parses", "[protocol][fixtures]")
{
    const auto fixtures = fixturesIn("invalid");
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& entry : fixtures)
    {
        DYNAMIC_SECTION(entry.label())
        {
            const auto root = loadFixture(entry.file);

            // "accept" means the only violation is a range one: the client parses the value and
            // relies on Ranges::clamp afterwards, so parsing must succeed here.
            if (requireExpectClient(root) == "accept")
            {
                const auto message = requireMessage(root);

                for (const auto& payload : requirePayloads(root))
                {
                    const auto pointer = payloadPointer(payload);
                    const auto as = payloadAs(payload);
                    INFO("pointer: '" << pointer.toStdString() << "' as: " << as);

                    const auto target = resolvePointer(message, pointer);
                    REQUIRE(target.has_value());

                    const auto outcome = parsePayload(*target, as);
                    INFO("parse error: " << outcome.error.toStdString());
                    REQUIRE(outcome.parsed);
                }
            }
        }
    }
}

TEST_CASE("Protocol fixtures: roleChanged, peerJoined, peerLeft and error declare no client payloads",
          "[protocol][fixtures]")
{
    const juce::StringArray payloadFreeTypes{"roleChanged", "peerJoined", "peerLeft", "error"};
    juce::StringArray covered;

    for (const auto& entry : allServerToClientFixtures())
    {
        const auto root = loadFixture(entry.file);
        const auto message = requireMessage(root);
        auto* messageObject = message.getDynamicObject();

        if (messageObject == nullptr || !messageObject->hasProperty("type"))
            continue;

        const auto type = messageObject->getProperty("type").toString();

        if (!payloadFreeTypes.contains(type))
            continue;

        INFO(entry.label());
        REQUIRE(requirePayloads(root).isEmpty());
        covered.addIfNotAlreadyThere(type);
    }

    for (const auto& type : payloadFreeTypes)
    {
        INFO("message type: " << type.toStdString());
        REQUIRE(covered.contains(type));
    }
}
