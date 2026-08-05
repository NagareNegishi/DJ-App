// Black-box tests for repository/LocalFileRepository.h, derived purely from the spec
// handed to this agent (no implementation source was read).
// Covers audio decoding, buffer caching (pointer identity across repeated
// getAudioBuffer calls), unknown-id handling, and header-only duration population
// before any decode occurs.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "repository/LocalFileRepository.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace {

juce::File makeUniqueTempDir (const juce::String& prefix)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile (prefix + juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}

struct TempDirFixture
{
    juce::File dir { makeUniqueTempDir ("djapp_repo_decode_test_") };
    ~TempDirFixture() { dir.deleteRecursively(); }
};

void writeManifest (const juce::File& rootDir, const juce::String& jsonText)
{
    rootDir.getChildFile ("manifest.json").replaceWithText (jsonText);
}

juce::File writeTestWav (const juce::File& dir, const juce::String& fileName,
                          int numChannels, int numSamples, double sampleRate)
{
    juce::File wavFile = dir.getChildFile (fileName);
    juce::WavAudioFormat wavFormat;

    auto outStream = wavFile.createOutputStream();
    REQUIRE (outStream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (outStream.get(), sampleRate,
                                    (unsigned int) numChannels, 16, {}, 0));
    REQUIRE (writer != nullptr);
    outStream.release(); // the writer now owns the stream

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = std::sin ((float) i * 0.05f) * 0.5f;
    }
    REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, numSamples));
    writer.reset();
    return wavFile;
}

} // namespace

TEST_CASE ("LocalFileRepository decodes a valid manifest entry's audio", "[repository]")
{
    TempDirFixture fx;
    constexpr int numChannels = 2;
    constexpr int numSamples = 4410; // 0.1s @ 44100Hz
    constexpr double sampleRate = 44100.0;
    writeTestWav (fx.dir, "demo1.wav", numChannels, numSamples, sampleRate);
    writeManifest (fx.dir,
        R"({ "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav", )"
        R"("bpm": 120 } ] })");

    djapp::LocalFileRepository repo (fx.dir);
    auto audio = repo.getAudioBuffer ("demo1");

    REQUIRE (audio != nullptr);
    CHECK (audio->sampleRate == Catch::Approx (sampleRate));
    CHECK (audio->buffer.getNumChannels() == numChannels);
    CHECK (audio->buffer.getNumSamples() == numSamples);
}

TEST_CASE ("LocalFileRepository caches decoded audio and returns the same instance on "
           "repeated calls",
           "[repository]")
{
    TempDirFixture fx;
    writeTestWav (fx.dir, "demo1.wav", 1, 4410, 44100.0);
    writeManifest (fx.dir,
        R"({ "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav" } ] })");

    djapp::LocalFileRepository repo (fx.dir);
    auto first = repo.getAudioBuffer ("demo1");
    auto second = repo.getAudioBuffer ("demo1");

    REQUIRE (first != nullptr);
    REQUIRE (second != nullptr);
    CHECK (first.get() == second.get());
}

TEST_CASE ("LocalFileRepository getAudioBuffer returns nullptr for an id never present "
           "in the manifest",
           "[repository]")
{
    TempDirFixture fx;
    writeTestWav (fx.dir, "demo1.wav", 1, 4410, 44100.0);
    writeManifest (fx.dir,
        R"({ "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav" } ] })");

    djapp::LocalFileRepository repo (fx.dir);
    REQUIRE (repo.getAudioBuffer ("nonexistent-id") == nullptr);
}

TEST_CASE ("LocalFileRepository populates track duration from the audio header before "
           "any decode call",
           "[repository]")
{
    TempDirFixture fx;
    constexpr int numSamples = 22050; // 0.5s @ 44100Hz
    constexpr double sampleRate = 44100.0;
    constexpr double expectedDuration = numSamples / sampleRate;
    writeTestWav (fx.dir, "demo1.wav", 1, numSamples, sampleRate);
    writeManifest (fx.dir,
        R"({ "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav" } ] })");

    // Construct the repository and read metadata WITHOUT ever calling getAudioBuffer
    // first: duration must already be correct from a cheap header read at list time,
    // not only after a full decode.
    djapp::LocalFileRepository repo (fx.dir);

    auto listed = repo.listAvailableTracks();
    REQUIRE (listed.size() == 1);
    CHECK (listed[0].durationSeconds > 0.0);
    CHECK (listed[0].durationSeconds == Catch::Approx (expectedDuration).margin (0.01));

    auto meta = repo.getTrackMetadata ("demo1");
    REQUIRE (meta.has_value());
    CHECK (meta->durationSeconds == Catch::Approx (expectedDuration).margin (0.01));
}
