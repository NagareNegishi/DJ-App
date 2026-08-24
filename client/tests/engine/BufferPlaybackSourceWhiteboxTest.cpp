// White-box additions for engine/BufferPlaybackSource.cpp, pinning internal
// branches not reachable from BufferPlaybackSourceTest.cpp's (black-box,
// IdentityTimeStretcher-default) or BufferPlaybackSourceStretchTest.cpp's
// (black-box, SignalsmithTimeStretcher-explicit) contract-level tests:
//
// - the capacity guard in getNextAudioBlock() (step 3 in the .cpp) that makes
//   "the destination is always fully written by the time this function
//   returns" hold even when prepareToPlay() was never called, or when a
//   caller requests more frames than prepareToPlay()'s scratch buffers were
//   sized for;
// - the mid-drain recovery branch (step 8's "else" case) where a pull that
//   unexpectedly finds real audio again (repeat toggled on while draining)
//   cancels the in-progress drain instead of forcing a stop. IdentityTimeStretcher
//   (outputLatencyFrames_ == 0) can never exercise this: its drain window is
//   exactly one output frame, always fully consumed within the same block it
//   starts in (drainFramesRemaining_ = 0 + 1, and any numOutput >= 1 finishes
//   it same-block - see step 8's "drainFramesRemaining_ = max(0,
//   drainFramesRemaining_ - numOutput)"), so there is never a "next block" in
//   which to intervene. A real stretcher's multi-hundred/thousand-sample
//   latency is what actually opens a multi-block window to test this branch in.

#include "engine/BufferPlaybackSource.h"
#include "engine/SignalsmithTimeStretcher.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>

namespace
{

std::shared_ptr<const djapp::LoadedAudio> makeSineAudio(int numSamples, double sampleRate, double freqHz,
                                                        int numChannels = 1, float amplitude = 0.8f)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        const float value = amplitude * (float)std::sin(phaseIncrement * (double)i);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, value);
    }
    return std::make_shared<const djapp::LoadedAudio>(djapp::LoadedAudio{std::move(buffer), sampleRate});
}

juce::AudioBuffer<float> renderBlock(djapp::BufferPlaybackSource& source, int numChannels, int numSamples)
{
    juce::AudioBuffer<float> scratch(numChannels, numSamples);
    scratch.clear();
    juce::AudioSourceChannelInfo info(&scratch, 0, numSamples);
    source.getNextAudioBlock(info);
    return scratch;
}

bool allSamplesSilent(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples, float margin = 1.0e-6f)
{
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            if (std::abs(buffer.getSample(ch, i)) > margin)
                return false;
    return true;
}

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(buffer.getSample(ch, i)))
                return false;
    return true;
}

float computeRms(const juce::AudioBuffer<float>& buffer, int channel, int numSamples)
{
    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double v = (double)buffer.getSample(channel, i);
        sumSquares += v * v;
    }
    return (float)std::sqrt(sumSquares / (double)numSamples);
}

} // namespace

TEST_CASE("BufferPlaybackSource getNextAudioBlock() before prepareToPlay() has ever been called renders "
          "silence and does not crash (pullScratch_ is still empty)",
          "[engine][BufferPlaybackSource][whitebox]")
{
    djapp::BufferPlaybackSource source; // default (IdentityTimeStretcher) constructor - not exercising this
                                        // guard's interaction with a real stretcher is fine, since the guard
                                        // itself fires before the stretcher is ever touched.
    source.load(makeSineAudio(4096, 44100.0, 440.0));
    source.setPlaying(true);

    // No prepareToPlay() call at all: pullScratch_/stretchOutScratch_ are
    // still default-constructed (empty), so the capacity guard's first
    // condition (pullScratch_[0].size() == 0) must be what saves this call,
    // not a coincidentally-large-enough default.
    auto block = renderBlock(source, 2, 512);

    CHECK(allSamplesSilent(block, 2, 512));
}

TEST_CASE("BufferPlaybackSource getNextAudioBlock() requesting more frames than prepareToPlay() sized "
          "scratch buffers for renders silence and does not crash or overflow",
          "[engine][BufferPlaybackSource][whitebox]")
{
    djapp::BufferPlaybackSource source;

    // capacityFrames = max(samplesPerBlockExpected, 512) * 4 + 4096 (see
    // BufferPlaybackSource.cpp's prepareToPlay): with samplesPerBlockExpected
    // = 64, that's max(64, 512) * 4 + 4096 = 6144.
    constexpr int samplesPerBlockExpected = 64;
    constexpr int capacityFrames = 6144;
    source.prepareToPlay(samplesPerBlockExpected, 44100.0);
    source.load(makeSineAudio(4 * capacityFrames, 44100.0, 440.0));
    source.setPlaying(true);

    // Comfortably past the sized capacity - the exact bound this guard's
    // second condition (numOutput > stretchOutScratch_[0].size()) exists to
    // catch, without this test needing to know the sizing formula beyond
    // what the .cpp itself documents.
    constexpr int oversizedNumOutput = capacityFrames + 1000;
    auto block = renderBlock(source, 2, oversizedNumOutput);

    CHECK(allSamplesSilent(block, 2, oversizedNumOutput));

    // The source must remain usable afterward - a defensive guard, not a
    // fatal condition - a normal, correctly-sized call right after must
    // still render real audio rather than staying wedged silent.
    auto normalBlock = renderBlock(source, 2, 512);
    CHECK_FALSE(allSamplesSilent(normalBlock, 2, 512));
}

TEST_CASE("BufferPlaybackSource with a real stretcher: repeat toggled on mid-drain cancels the drain and "
          "resumes audible playback instead of forcing a stop",
          "[engine][BufferPlaybackSource][TimeStretcher][whitebox]")
{
    // Regression-shaped white-box test for the .cpp's step 8 "else" branch:
    // "A mid-drain block whose pull unexpectedly found real audio again (e.g.
    // repeat/loop toggled on while draining) - cancel the drain, playback
    // continues." BufferPlaybackSourceStretchTest.cpp's existing drain test
    // covers the repeat-stays-false path (drains, then genuinely stops); this
    // test covers the other side of that same branch, which nothing else
    // exercises.
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 20000; // ~453ms, same length as the drain test this complements
    constexpr int blockSize = 512;
    // Matches BufferPlaybackSourceStretchTest.cpp's own estimate: the raw
    // pull exhausts the source after roughly this many blocks.
    constexpr int approxDryBlock = sourceLength / blockSize; // ~39

    djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setRepeat(false); // starts un-repeating, so the raw pull runs dry and a drain begins
    source.setPlaying(true);

    // Render up to just past the estimated dry point (still comfortably
    // inside the drain window the companion drain-to-stop test measured as
    // lasting until at least approxDryBlock - 2, and never completing before
    // approxDryBlock + 60) - i.e. a drain should be in progress here, but not
    // yet finished.
    for (int b = 0; b < approxDryBlock + 3; ++b)
    {
        renderBlock(source, 2, blockSize);
        REQUIRE(source.isPlaying()); // must not have stopped yet - still mid-drain, not post-drain
    }

    // Intervene mid-drain: re-enable repeat. The next pull should find the
    // wrap-around boundary and recover real audio instead of running dry
    // again, which per step 8's else-branch must cancel the drain rather
    // than let it continue counting down toward a forced stop.
    source.setRepeat(true);

    // Render well past where the companion drain-to-stop test's generous
    // upper bound (approxDryBlock + 60) would have forced a stop if the
    // drain had NOT been cancelled - if this branch were broken (e.g. the
    // drain kept counting down regardless of recovered audio), playback
    // would have self-stopped by now.
    bool sawAudibleContentAfterRecovery = false;
    for (int b = 0; b < 120; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        REQUIRE(allSamplesFinite(block, 2, blockSize));
        REQUIRE(source.isPlaying()); // must NOT have self-stopped: the drain was cancelled, not merely delayed
        if (computeRms(block, 0, blockSize) > 0.05f)
            sawAudibleContentAfterRecovery = true;
    }

    CHECK(sawAudibleContentAfterRecovery);
}
