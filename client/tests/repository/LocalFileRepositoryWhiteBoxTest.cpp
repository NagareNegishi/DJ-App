// White-box tests for repository/LocalFileRepository.cpp, written after reading its
// actual implementation. Targets internals the black-box suite (LocalFileRepositoryTest.cpp,
// LocalFileRepositoryDecodeTest.cpp) could not know to aim at:
//   - the exact maxDecodedSampleCount bound (48000*3600*2 = 345,600,000 total samples)
//     used by getAudioBuffer() to reject oversized decodes, tested right at and just
//     over the boundary via a WAV file whose header declares a data-chunk length far
//     larger than the file's real byte length (truncated/corrupt-file shape);
//   - that this size bound applies only to getAudioBuffer(), not to the header-only
//     duration read done by parseEntry() at manifest-load time (listAvailableTracks()
//     still lists an entry whose real decode would be rejected);
//   - the id-regex character class (allowed punctuation, a disallowed non-space/slash
//     character) beyond what the black-box boundary tests exercised;
//   - isBareFilename()'s empty-string case;
//   - that the symlink check rejects every symlink, including one whose target is
//     inside rootDir (not just ones that escape it);
//   - bpm parsing rejecting non-positive values and wrong JSON types.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "repository/LocalFileRepository.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace {

juce::File makeUniqueTempDir(const juce::String& prefix)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(prefix + juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}

struct TempDirFixture
{
    juce::File dir{ makeUniqueTempDir("djapp_repo_wb_test_") };
    ~TempDirFixture() { dir.deleteRecursively(); }
};

void writeManifest(const juce::File& rootDir, const juce::String& jsonText)
{
    rootDir.getChildFile("manifest.json").replaceWithText(jsonText);
}

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

void writeLE32(juce::MemoryOutputStream& out, juce::uint32 v)
{
    out.writeByte((char)(v & 0xff));
    out.writeByte((char)((v >> 8) & 0xff));
    out.writeByte((char)((v >> 16) & 0xff));
    out.writeByte((char)((v >> 24) & 0xff));
}

void writeLE16(juce::MemoryOutputStream& out, juce::uint16 v)
{
    out.writeByte((char)(v & 0xff));
    out.writeByte((char)((v >> 8) & 0xff));
}

// Writes a canonical 44-byte PCM WAV header whose "data" chunk declares
// `declaredDataBytes`, then truncates the file immediately after the header -
// no real PCM payload follows. juce::WavAudioFormatReader computes
// lengthInSamples purely from this declared chunk size (see
// juce_WavAudioFormat.cpp's "data" chunk handling), not from the stream's
// actual remaining length, so this reader-visible lengthInSamples can be made
// arbitrarily large without writing gigabytes of real audio to disk.
juce::File writeHeaderOnlyWavClaiming(const juce::File& dir, const juce::String& fileName, juce::uint32 declaredDataBytes,
                                       int numChannels = 1, juce::uint32 sampleRate = 44100, int bitsPerSample = 16)
{
    juce::File wavFile = dir.getChildFile(fileName);
    juce::MemoryOutputStream header;

    const juce::uint32 blockAlign = (juce::uint32)(numChannels * bitsPerSample / 8);
    const juce::uint32 byteRate = sampleRate * blockAlign;

    header.write("RIFF", 4);
    writeLE32(header, 36 + 0); // RIFF size: header content only, real file is truncated right after
    header.write("WAVE", 4);

    header.write("fmt ", 4);
    writeLE32(header, 16);
    writeLE16(header, 1); // PCM
    writeLE16(header, (juce::uint16)numChannels);
    writeLE32(header, sampleRate);
    writeLE32(header, byteRate);
    writeLE16(header, (juce::uint16)blockAlign);
    writeLE16(header, (juce::uint16)bitsPerSample);

    header.write("data", 4);
    writeLE32(header, declaredDataBytes); // the lie: far exceeds what actually follows

    REQUIRE(wavFile.replaceWithData(header.getData(), header.getDataSize()));
    return wavFile;
}

} // namespace

TEST_CASE("LocalFileRepository getAudioBuffer rejects a decode exactly one sample over the "
          "size bound",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    // maxDecodedSampleCount = 48000*3600*2 = 345,600,000 total samples (mono, so
    // lengthInSamples == totalSamples here); declare one sample beyond it.
    constexpr juce::int64 overBound = 345600000LL + 1LL;
    constexpr int bytesPerSample = 2;
    writeHeaderOnlyWavClaiming(fx.dir, "huge.wav", (juce::uint32)(overBound * bytesPerSample));
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "huge", "title": "Huge", "file": "huge.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    // The size-bound check runs before any allocation/read, so this is cheap even
    // though the header claims ~660MB of audio.
    REQUIRE(repo.getAudioBuffer("huge") == nullptr);
}

TEST_CASE("LocalFileRepository getAudioBuffer accepts a decode exactly at the size bound",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    constexpr juce::int64 atBound = 345600000LL;
    constexpr int bytesPerSample = 2;
    writeHeaderOnlyWavClaiming(fx.dir, "atbound.wav", (juce::uint32)(atBound * bytesPerSample));
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "atbound", "title": "AtBound", "file": "atbound.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    auto audio = repo.getAudioBuffer("atbound");

    // Exactly at the bound must NOT be rejected: the check in LocalFileRepository.cpp
    // is `totalSamples > maxDecodedSampleCount`, a strict greater-than.
    REQUIRE(audio != nullptr);
    CHECK(audio->buffer.getNumSamples() == (int)atBound);
}

TEST_CASE("LocalFileRepository lists an oversized track's header-only duration even though "
          "getAudioBuffer rejects decoding it",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    // Comfortably over the decode bound.
    constexpr juce::int64 overBound = 345600000LL * 2LL;
    constexpr int bytesPerSample = 2;
    constexpr juce::uint32 sampleRate = 44100;
    writeHeaderOnlyWavClaiming(fx.dir, "huge2.wav", (juce::uint32)(overBound * bytesPerSample), 1, sampleRate);
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "huge2", "title": "Huge Two", "file": "huge2.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);

    // parseEntry()'s header-only read (used for listAvailableTracks/getTrackMetadata)
    // has no size bound of its own: it only rejects on sampleRate <= 0. The entry is
    // listed with a (very large) duration derived straight from the lying header.
    auto listed = repo.listAvailableTracks();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].durationSeconds > 1000.0);

    // getAudioBuffer(), which re-opens the file and applies maxDecodedSampleCount,
    // rejects the same track outright.
    REQUIRE(repo.getAudioBuffer("huge2") == nullptr);
}

TEST_CASE("LocalFileRepository accepts manifest ids using every allowed punctuation character",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
    const juce::String id = "a.b_c-D9";
    writeManifest(fx.dir, R"({ "tracks": [ { "id": ")" + id + R"(", "title": "T", "file": "track.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().size() == 1);
    REQUIRE(repo.getTrackMetadata(id).has_value());
}

TEST_CASE("LocalFileRepository rejects a manifest id containing a character outside the "
          "allowed set that is neither whitespace nor a path separator",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
    // '@' is not in "A-Za-z0-9._-"; not covered by the black-box suite's space/slash
    // cases, which could pass for a check that merely rejects whitespace and path
    // separators rather than enforcing the full allow-list.
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "bad@id", "title": "T", "file": "track.wav" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().empty());
    REQUIRE_FALSE(repo.getTrackMetadata("bad@id").has_value());
}

TEST_CASE("LocalFileRepository rejects a manifest entry whose file name is an empty string",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    writeManifest(fx.dir, R"({ "tracks": [ { "id": "emptyfile", "title": "T", "file": "" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    REQUIRE(repo.listAvailableTracks().empty());
    REQUIRE_FALSE(repo.getTrackMetadata("emptyfile").has_value());
}

TEST_CASE("LocalFileRepository rejects a symlink even when its target lives inside rootDir",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    // The black-box suite only exercises a symlink escaping rootDir (where rejection
    // could plausibly come from the isAChildOf() check alone rather than an explicit
    // blanket symlink rejection). Point the link at a file that is itself a valid,
    // in-root, otherwise-acceptable track to isolate the isSymbolicLink() check.
    auto target = writeTestWav(fx.dir, "real.wav", 1, 100, 44100.0);
    auto linkFile = fx.dir.getChildFile("alias.wav");
    const bool linked = target.createSymbolicLink(linkFile, true);
    if (!linked)
        SKIP("Symbolic links are not supported in this test environment");

    writeManifest(fx.dir,
                  R"({ "tracks": [
                        { "id": "real", "title": "Real", "file": "real.wav" },
                        { "id": "aliased", "title": "Aliased", "file": "alias.wav" }
                      ] })");

    djapp::LocalFileRepository repo(fx.dir);
    // The genuine, non-symlink entry loads fine...
    REQUIRE(repo.getTrackMetadata("real").has_value());
    // ...but the in-root symlink to that same valid file is still rejected outright.
    REQUIRE_FALSE(repo.getTrackMetadata("aliased").has_value());
    REQUIRE(repo.listAvailableTracks().size() == 1);
}

TEST_CASE("LocalFileRepository ignores non-positive bpm values", "[repository][whitebox]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);

    SECTION("bpm of zero")
    {
        writeManifest(fx.dir,
                      R"({ "tracks": [ { "id": "t1", "title": "T", "file": "track.wav", "bpm": 0 } ] })");
        djapp::LocalFileRepository repo(fx.dir);
        auto meta = repo.getTrackMetadata("t1");
        REQUIRE(meta.has_value());
        CHECK_FALSE(meta->bpm.has_value());
    }

    SECTION("negative bpm")
    {
        writeManifest(fx.dir,
                      R"({ "tracks": [ { "id": "t2", "title": "T", "file": "track.wav", "bpm": -128 } ] })");
        djapp::LocalFileRepository repo(fx.dir);
        auto meta = repo.getTrackMetadata("t2");
        REQUIRE(meta.has_value());
        CHECK_FALSE(meta->bpm.has_value());
    }
}

TEST_CASE("LocalFileRepository ignores a bpm value given as the wrong JSON type",
          "[repository][whitebox]")
{
    TempDirFixture fx;
    writeTestWav(fx.dir, "track.wav", 1, 4410, 44100.0);
    writeManifest(fx.dir,
                  R"({ "tracks": [ { "id": "t3", "title": "T", "file": "track.wav", "bpm": "120" } ] })");

    djapp::LocalFileRepository repo(fx.dir);
    // A string bpm fails the isDouble()/isInt()/isInt64() type check; the entry
    // itself is still accepted (bpm is optional), just without a bpm value.
    auto meta = repo.getTrackMetadata("t3");
    REQUIRE(meta.has_value());
    CHECK_FALSE(meta->bpm.has_value());
}
