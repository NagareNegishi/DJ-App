// Black-box tests for repository/LocalFileRepository.h, derived purely from the spec
// handed to this agent (no implementation source was read).
// Covers manifest-entry validation (id pattern, unsafe file paths, symlinks, missing
// and undecodable files), malformed/missing manifest handling, and the
// no-exceptions-escape contract of AudioRepository.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "repository/LocalFileRepository.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace
{

juce::File makeUniqueTempDir(const juce::String& prefix)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(prefix + juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}

// RAII fixture directory, unique per instantiation, deleted on scope exit.
struct TempDirFixture
{
    juce::File dir{makeUniqueTempDir("djapp_repo_test_")};
    ~TempDirFixture() { dir.deleteRecursively(); }
};

void writeManifest(const juce::File& rootDir, const juce::String& jsonText)
{
    rootDir.getChildFile("manifest.json").replaceWithText(jsonText);
}

void writeGarbageFile(const juce::File& dir, const juce::String& fileName)
{
    dir.getChildFile(fileName).replaceWithText("this is not audio data, just garbage text pretending to be a wav file");
}

// Writes a small generated WAV file into `dir` and returns the file.
juce::File writeTestWav(const juce::File& dir, const juce::String& fileName, int numChannels, int numSamples,
                        double sampleRate)
{
    juce::File wavFile = dir.getChildFile(fileName);
    juce::WavAudioFormat wavFormat;

    auto outStream = wavFile.createOutputStream();
    REQUIRE(outStream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(outStream.get(), sampleRate, (unsigned int)numChannels, 16, {}, 0));
    REQUIRE(writer != nullptr);
    outStream.release(); // the writer now owns the stream

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::sin((float)i * 0.05f) * 0.5f;
    }
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numSamples));
    writer.reset();
    return wavFile;
}

} // namespace

TEST_CASE("LocalFileRepository rejects manifest entries whose id fails the id pattern", "[repository]")
{
    SECTION("empty id")
    {
        TempDirFixture fx;
        writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "", "title": "T", "file": "track.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("").has_value());
    }

    SECTION("id longer than 64 characters")
    {
        TempDirFixture fx;
        writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
        const juce::String longId(std::string(65, 'a'));
        writeManifest(fx.dir, R"({ "tracks": [ { "id": ")" + longId + R"(", "title": "T", "file": "track.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata(longId).has_value());
    }

    SECTION("id containing a space")
    {
        TempDirFixture fx;
        writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad id", "title": "T", "file": "track.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad id").has_value());
    }

    SECTION("id containing a slash")
    {
        TempDirFixture fx;
        writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad/id", "title": "T", "file": "track.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad/id").has_value());
    }
}

TEST_CASE("LocalFileRepository accepts an id at the 64-character boundary", "[repository]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
    const juce::String maxId(std::string(64, 'a'));
    writeManifest(fx.dir, R"({ "tracks": [ { "id": ")" + maxId + R"(", "title": "T", "file": "track.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().size() == 1);
    REQUIRE(repo.getTrackMetadata(maxId).has_value());
}

TEST_CASE("LocalFileRepository rejects manifest entries with unsafe file paths", "[repository]")
{
    SECTION("file containing '..'")
    {
        TempDirFixture fx;
        auto outsideDir = makeUniqueTempDir("djapp_repo_test_outside_");
        auto sibling = writeTestWav(outsideDir, "evil.wav", 1, 100, 44100.0);
        // Reference it via a manifest string that walks up out of the fixture root.
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad1", "title": "T", "file": "../)" + outsideDir.getFileName() +
                                  "/" + sibling.getFileName() + R"(" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad1").has_value());

        outsideDir.deleteRecursively();
    }

    SECTION("file containing a forward slash (subdirectory)")
    {
        TempDirFixture fx;
        auto subDir = fx.dir.getChildFile("sub");
        subDir.createDirectory();
        writeTestWav(subDir, "track.wav", 1, 100, 44100.0);
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad2", "title": "T", "file": "sub/track.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad2").has_value());
    }

    SECTION("file containing a backslash")
    {
        TempDirFixture fx;
        writeTestWav(fx.dir, "a\\b.wav", 1, 100, 44100.0); // literal backslash, valid on Linux
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad3", "title": "T", "file": "a\\b.wav" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad3").has_value());
    }

    SECTION("file given as an absolute path")
    {
        TempDirFixture fx;
        writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad4", "title": "T", "file": "/etc/passwd" } ] })");

        djapp::LocalFileRepository repo(fx.dir);
        REQUIRE(repo.listAvailableTracks().empty());
        REQUIRE_FALSE(repo.getTrackMetadata("bad4").has_value());
    }
}

TEST_CASE("LocalFileRepository rejects a manifest entry whose file is a symlink", "[repository]")
{
    TempDirFixture fx;
    // Target lives outside the fixture root, so following the symlink would be a
    // traversal attempt in disguise.
    auto outsideDir = makeUniqueTempDir("djapp_repo_test_outside_");
    auto target = writeTestWav(outsideDir, "target.wav", 1, 100, 44100.0);

    auto linkFile = fx.dir.getChildFile("linked.wav");
    const bool linked = target.createSymbolicLink(linkFile, true);
    if (!linked)
    {
        outsideDir.deleteRecursively();
        SKIP("Symbolic links are not supported in this test environment");
    }

    writeManifest(fx.dir, R"({ "tracks": [ { "id": "symlinked", "title": "T", "file": "linked.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().empty());
    REQUIRE_FALSE(repo.getTrackMetadata("symlinked").has_value());
    REQUIRE(repo.getAudioBuffer("symlinked") == nullptr);

    outsideDir.deleteRecursively();
}

TEST_CASE("LocalFileRepository rejects a manifest entry whose file does not exist on disk", "[repository]")
{
    TempDirFixture fx;
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "missing1", "title": "T", "file": "nope.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().empty());
    REQUIRE_FALSE(repo.getTrackMetadata("missing1").has_value());
    REQUIRE(repo.getAudioBuffer("missing1") == nullptr);
}

TEST_CASE("LocalFileRepository getAudioBuffer never crashes on an undecodable file", "[repository]")
{
    TempDirFixture fx;
    writeGarbageFile(fx.dir, "garbage.wav");
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "garbage1", "title": "T", "file": "garbage.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);

    // Whether the entry is list-visible (duration 0) is unspecified for this case per
    // spec; only the getAudioBuffer contract is pinned here.
    REQUIRE(repo.getAudioBuffer("garbage1") == nullptr);
}

TEST_CASE("LocalFileRepository loads valid entries even when another entry in the same "
          "manifest is rejected",
          "[repository]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "good1.wav", 1, 4410, 44100.0);
    writeTestWav(fx.dir, "good2.wav", 2, 8820, 44100.0);

    writeManifest(fx.dir, R"({ "tracks": [
        { "id": "good1", "title": "Good One", "file": "good1.wav", "bpm": 120 },
        { "id": "bad space id", "title": "Bad", "file": "good1.wav" },
        { "id": "good2", "title": "Good Two", "file": "good2.wav" }
    ] })");

    djapp::LocalFileRepository repo(fx.dir);
    auto tracks = repo.listAvailableTracks();
    REQUIRE(tracks.size() == 2);

    auto good1 = repo.getTrackMetadata("good1");
    REQUIRE(good1.has_value());
    CHECK(good1->title == "Good One");
    CHECK(good1->fileName == "good1.wav");
    REQUIRE(good1->bpm.has_value());
    CHECK(good1->bpm.value() == Catch::Approx(120.0));

    auto good2 = repo.getTrackMetadata("good2");
    REQUIRE(good2.has_value());
    CHECK(good2->title == "Good Two");
    CHECK_FALSE(good2->bpm.has_value());

    REQUIRE_FALSE(repo.getTrackMetadata("bad space id").has_value());
}

TEST_CASE("LocalFileRepository handles a missing or unparseable manifest without crashing", "[repository]")
{
    SECTION("no manifest.json present")
    {
        TempDirFixture fx;
        std::unique_ptr<djapp::LocalFileRepository> repo;
        REQUIRE_NOTHROW(repo = std::make_unique<djapp::LocalFileRepository>(fx.dir));
        REQUIRE(repo->listAvailableTracks().empty());
        REQUIRE_FALSE(repo->getTrackMetadata("anything").has_value());
        REQUIRE(repo->getAudioBuffer("anything") == nullptr);
    }

    SECTION("manifest.json contains invalid JSON")
    {
        TempDirFixture fx;
        writeManifest(fx.dir, "{ this is not : valid json at all !!");
        std::unique_ptr<djapp::LocalFileRepository> repo;
        REQUIRE_NOTHROW(repo = std::make_unique<djapp::LocalFileRepository>(fx.dir));
        REQUIRE(repo->listAvailableTracks().empty());
    }

    SECTION("manifest.json is valid JSON but an empty object")
    {
        TempDirFixture fx;
        writeManifest(fx.dir, "{}");
        std::unique_ptr<djapp::LocalFileRepository> repo;
        REQUIRE_NOTHROW(repo = std::make_unique<djapp::LocalFileRepository>(fx.dir));
        REQUIRE(repo->listAvailableTracks().empty());
    }

    SECTION("manifest.json has 'tracks' as a non-array value")
    {
        TempDirFixture fx;
        writeManifest(fx.dir, R"({ "tracks": "not an array" })");
        std::unique_ptr<djapp::LocalFileRepository> repo;
        REQUIRE_NOTHROW(repo = std::make_unique<djapp::LocalFileRepository>(fx.dir));
        REQUIRE(repo->listAvailableTracks().empty());
    }
}
