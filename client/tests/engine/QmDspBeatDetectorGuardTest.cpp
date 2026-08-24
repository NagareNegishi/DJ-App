// White-box addition for engine/QmDspBeatDetector.cpp: pins the early-return
// guard clause at the top of analyze() -
//   if (audio.sampleRate <= 0.0 || audio.buffer.getNumSamples() <= 0 ||
//       audio.buffer.getNumChannels() <= 0)
//       return BeatGrid{};
// - which BeatDetectorTest.cpp's black-box suite never actually reaches: its
// "too short"/"silent" cases all use a positive sample rate and a real
// (nonzero-channel, nonzero-sample) buffer, so they return BeatGrid{} via the
// *different* code path of running the real detection-function/tempo-tracker
// pipeline and finding beats.size() < 2 - not via this guard. That matters
// because the guard exists specifically to avoid ever constructing
// DetectionFunction/TempoTrackV2 (both sized from the buffer) with a
// degenerate 0-length or 0-channel input, which is a different risk (a
// construction-time crash/UB in the vendored library) than "ran the pipeline
// and got an inconclusive answer".

#include <catch2/catch_test_macros.hpp>

#include "engine/QmDspBeatDetector.h"
#include "model/BeatGrid.h"
#include "repository/AudioRepository.h"

#include <juce_audio_basics/juce_audio_basics.h>

TEST_CASE("QmDspBeatDetector returns BeatGrid{} via the guard clause for sampleRate == 0.0, without "
          "running the detection pipeline",
          "[BeatDetector][qm-dsp][whitebox]")
{
    djapp::LoadedAudio audio;
    audio.sampleRate = 0.0;
    audio.buffer.setSize(1, 44100); // otherwise a perfectly normal 1s mono buffer
    audio.buffer.clear();

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} via the guard clause for a negative sampleRate",
          "[BeatDetector][qm-dsp][whitebox]")
{
    djapp::LoadedAudio audio;
    audio.sampleRate = -44100.0;
    audio.buffer.setSize(1, 44100);
    audio.buffer.clear();

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} via the guard clause for a buffer with zero samples "
          "(nonzero channels, valid sample rate)",
          "[BeatDetector][qm-dsp][whitebox]")
{
    djapp::LoadedAudio audio;
    audio.sampleRate = 44100.0;
    audio.buffer.setSize(1, 0); // zero-length, but not default-constructed (exercises getNumSamples() <= 0
                                // specifically, independent of getNumChannels())
    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} via the guard clause for a buffer with zero channels "
          "(nonzero samples, valid sample rate)",
          "[BeatDetector][qm-dsp][whitebox]")
{
    djapp::LoadedAudio audio;
    audio.sampleRate = 44100.0;
    audio.buffer.setSize(0, 44100); // zero channels, exercising getNumChannels() <= 0 specifically

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}

TEST_CASE("QmDspBeatDetector returns BeatGrid{} for a fully default-constructed LoadedAudio (all three "
          "guard conditions at once)",
          "[BeatDetector][qm-dsp][whitebox]")
{
    djapp::LoadedAudio audio; // sampleRate == 0, buffer has 0 channels and 0 samples

    djapp::QmDspBeatDetector detector;
    djapp::BeatGrid grid = detector.analyze(audio);

    CHECK(grid.bpm == 0.0);
    CHECK(grid.firstBeatSeconds == 0.0);
}
