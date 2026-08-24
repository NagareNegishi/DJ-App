// Black-box tests for engine/QmDspBeatDetector.h and engine/NullBeatDetector.h,
// derived purely from the spec handed to this agent (no implementation source
// was read).
//
// BeatDetector::analyze(const LoadedAudio&) -> BeatGrid{bpm, firstBeatSeconds}.
// Per the spec: QmDspBeatDetector derives bpm itself from the median inter-beat
// gap of qm-dsp's detected beat frames (bpm = 60.0 / median-interval-seconds);
// firstBeatSeconds = beats[0] / sampleRate; a buffer too short to produce two
// beats, or silence, must fall back to BeatGrid{} (bpm stays 0).
// NullBeatDetector is a pure test double: returns a caller-supplied BeatGrid
// (default BeatGrid{}) regardless of the audio passed in.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/NullBeatDetector.h"
#include "engine/QmDspBeatDetector.h"
#include "model/BeatGrid.h"
#include "repository/AudioRepository.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace
{

// Builds a synthetic click-track: single-sample impulses (amplitude 1.0) at
// regular beatInterval spacing, starting at firstBeatSeconds, on every
// channel. Everything else is silence. This is the "small synthetic buffer,
// e.g. click-track signal" style called out by docs/plan/05-testing.md.
djapp::LoadedAudio makeClickTrack(double sampleRate, double bpm, double firstBeatSeconds, int numBeats,
                                  int numChannels = 1, double tailSeconds = 0.5)
{
    const double beatIntervalSeconds = 60.0 / bpm;
    const double totalDurationSeconds = firstBeatSeconds + (double)(numBeats - 1) * beatIntervalSeconds + tailSeconds;
    const int numSamples = (int)std::ceil(totalDurationSeconds * sampleRate);

    djapp::LoadedAudio audio;
    audio.sampleRate = sampleRate;
    audio.buffer.setSize(numChannels, numSamples);
    audio.buffer.clear();

    for (int beat = 0; beat < numBeats; ++beat)
    {
        const double beatTimeSeconds = firstBeatSeconds + (double)beat * beatIntervalSeconds;
        const int sampleIndex = (int)std::llround(beatTimeSeconds * sampleRate);
        if (sampleIndex >= 0 && sampleIndex < numSamples)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                audio.buffer.setSample(ch, sampleIndex, 1.0f);
        }
    }
    return audio;
}

djapp::LoadedAudio makeSilence(double sampleRate, double durationSeconds, int numChannels = 1)
{
    djapp::LoadedAudio audio;
    audio.sampleRate = sampleRate;
    const int numSamples = std::max(1, (int)(durationSeconds * sampleRate));
    audio.buffer.setSize(numChannels, numSamples);
    audio.buffer.clear();
    return audio;
}

} // namespace

TEST_CASE("QmDspBeatDetector recovers BPM and first-beat position from a mono 120 BPM click track",
          "[BeatDetector][qm-dsp]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double trueBpm = 120.0;
    constexpr double trueFirstBeatSeconds = 0.3;
    // qm-dsp's TempoTrackV2 needs at least ~641 detection-function frames
    // (winlen=512, step=128 in its own beat-period loop) to produce more than
    // one rcfmat column; below that, viterbi_decode bails out early and
    // calculateBeats degenerates to a single beat, which this class then
    // reports as a failed detection. 12 beats (6.3s) fell just short of that;
    // 18 beats (9.3s) clears it with a comfortable margin.
    constexpr int numBeats = 18;

    djapp::LoadedAudio audio = makeClickTrack(sampleRate, trueBpm, trueFirstBeatSeconds, numBeats, /*numChannels*/ 1);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    // "a few percent" tolerance per the spec's requested test shape.
    REQUIRE(grid.bpm == Catch::Approx(trueBpm).epsilon(0.05));
    // firstBeatSeconds should land close to the true first impulse, well
    // inside half a beat interval (0.25s at 120 BPM) so it can't be confused
    // with an adjacent beat.
    REQUIRE(grid.firstBeatSeconds == Catch::Approx(trueFirstBeatSeconds).margin(0.15));
}

TEST_CASE("QmDspBeatDetector recovers BPM from a stereo 120 BPM click track (mono-mixed internally)",
          "[BeatDetector][qm-dsp]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double trueBpm = 120.0;
    constexpr double trueFirstBeatSeconds = 0.3;
    // Same qm-dsp minimum-length requirement as the mono 120 BPM case above.
    constexpr int numBeats = 18;

    djapp::LoadedAudio audio = makeClickTrack(sampleRate, trueBpm, trueFirstBeatSeconds, numBeats, /*numChannels*/ 2);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    REQUIRE(grid.bpm == Catch::Approx(trueBpm).epsilon(0.05));
    REQUIRE(grid.firstBeatSeconds == Catch::Approx(trueFirstBeatSeconds).margin(0.15));
}

TEST_CASE("QmDspBeatDetector recovers a different BPM (90 BPM) to confirm the derivation isn't hardcoded",
          "[BeatDetector][qm-dsp]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double trueBpm = 90.0;
    constexpr double trueFirstBeatSeconds = 0.2;
    constexpr int numBeats = 12;

    djapp::LoadedAudio audio = makeClickTrack(sampleRate, trueBpm, trueFirstBeatSeconds, numBeats, /*numChannels*/ 1);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    REQUIRE(grid.bpm == Catch::Approx(trueBpm).epsilon(0.05));
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} (bpm == 0) for a buffer too short to produce two beats",
          "[BeatDetector][qm-dsp]")
{
    // A single click and nothing else: at most one beat can be found, so no
    // inter-beat interval exists to derive a BPM from.
    constexpr double sampleRate = 44100.0;
    djapp::LoadedAudio audio = makeClickTrack(sampleRate, /*bpm*/ 120.0, /*firstBeatSeconds*/ 0.1, /*numBeats*/ 1,
                                              /*numChannels*/ 1, /*tailSeconds*/ 0.3);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} (bpm == 0) for a very short buffer", "[BeatDetector][qm-dsp]")
{
    constexpr double sampleRate = 44100.0;
    djapp::LoadedAudio audio = makeSilence(sampleRate, /*durationSeconds*/ 0.01);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} (bpm == 0) for a longer entirely-silent buffer",
          "[BeatDetector][qm-dsp]")
{
    constexpr double sampleRate = 44100.0;
    djapp::LoadedAudio audio = makeSilence(sampleRate, /*durationSeconds*/ 5.0);

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("NullBeatDetector default-constructed returns BeatGrid{} regardless of the audio passed in",
          "[BeatDetector][null]")
{
    djapp::NullBeatDetector detector;

    // The audio content must not matter: pass a trivially small buffer.
    djapp::LoadedAudio dummyAudio;
    dummyAudio.sampleRate = 44100.0;
    dummyAudio.buffer.setSize(1, 1);
    dummyAudio.buffer.clear();

    djapp::BeatGrid grid = detector.analyze(dummyAudio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("NullBeatDetector constructed with a specific BeatGrid returns exactly that grid", "[BeatDetector][null]")
{
    djapp::BeatGrid expectedGrid;
    expectedGrid.bpm = 140.0;
    expectedGrid.firstBeatSeconds = 3.75;

    djapp::NullBeatDetector detector(expectedGrid);

    djapp::LoadedAudio dummyAudio;
    dummyAudio.sampleRate = 44100.0;
    dummyAudio.buffer.setSize(2, 100);
    dummyAudio.buffer.clear();

    djapp::BeatGrid grid = detector.analyze(dummyAudio);

    CHECK(grid.bpm == expectedGrid.bpm);
    CHECK(grid.firstBeatSeconds == expectedGrid.firstBeatSeconds);

    // Calling analyze() again with different audio must still return the
    // same stored grid, confirming it truly ignores its argument.
    djapp::LoadedAudio otherDummyAudio;
    otherDummyAudio.sampleRate = 48000.0;
    otherDummyAudio.buffer.setSize(1, 50000);
    otherDummyAudio.buffer.clear();
    djapp::BeatGrid gridAgain = detector.analyze(otherDummyAudio);

    CHECK(gridAgain.bpm == expectedGrid.bpm);
    CHECK(gridAgain.firstBeatSeconds == expectedGrid.firstBeatSeconds);
}
