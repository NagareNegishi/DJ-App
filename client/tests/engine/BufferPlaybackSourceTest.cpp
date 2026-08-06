#include "engine/BufferPlaybackSource.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace
{

// Sample value == sample index gives an unambiguous, cheap-to-check signal:
// any read offset, wrap, or channel mixup shows up as a wrong number.
std::shared_ptr<const djapp::LoadedAudio> makeRampAudio(int numSamples, double sampleRate, int numChannels = 1)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample(ch, i, (float)i);

    return std::make_shared<const djapp::LoadedAudio>(djapp::LoadedAudio{std::move(buffer), sampleRate});
}

// Every sample the same value identifies which of several successively
// loaded buffers is currently being rendered, with no dependence on read
// position: useful for pinning buffer-handoff behavior across repeated load().
std::shared_ptr<const djapp::LoadedAudio> makeConstantAudio(float value, int numSamples, double sampleRate,
                                                            int numChannels = 1)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample(ch, i, value);

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

constexpr float kContentMargin = 0.01f;
constexpr double kPositionMargin = 1.0e-4;
constexpr float kSilenceMargin = 1.0e-6f;

} // namespace

TEST_CASE("BufferPlaybackSource reproduces source samples at rate 1.0", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 64;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    auto block = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(block.getSample(0, i) == Catch::Approx((float)i).margin(kContentMargin));

    CHECK(source.getCurrentPositionSeconds() == Catch::Approx((double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource advances position twice as fast at rate 2.0", "[engine][BufferPlaybackSource]")
{
    // Sample rate chosen so seconds convert to sample counts with no rounding.
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;

    djapp::BufferPlaybackSource rate1;
    rate1.prepareToPlay(blockSize, sampleRate);
    rate1.load(makeRampAudio(sourceLength, sampleRate));
    rate1.setPlaybackRate(1.0f);
    rate1.setPlaying(true);
    renderBlock(rate1, 2, blockSize);

    djapp::BufferPlaybackSource rate2;
    rate2.prepareToPlay(blockSize, sampleRate);
    rate2.load(makeRampAudio(sourceLength, sampleRate));
    rate2.setPlaybackRate(2.0f);
    rate2.setPlaying(true);
    renderBlock(rate2, 2, blockSize);

    const double pos1 = rate1.getCurrentPositionSeconds();
    const double pos2 = rate2.getCurrentPositionSeconds();

    CHECK(pos2 == Catch::Approx(2.0 * pos1).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource consumes a source in half as many blocks at rate 2.0", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    // Not a multiple of blockSize so end-of-buffer is crossed mid-block, not exactly
    // on a block boundary, avoiding ambiguity about when isPlaying() flips.
    constexpr int sourceLength = 380;
    constexpr int blockSize = 100;

    auto blocksUntilStopped = [&](float rate)
    {
        djapp::BufferPlaybackSource source;
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeRampAudio(sourceLength, sampleRate));
        source.setPlaybackRate(rate);
        source.setPlaying(true);

        int blocks = 0;
        while (source.isPlaying() && blocks < 100)
        {
            renderBlock(source, 2, blockSize);
            ++blocks;
        }
        return blocks;
    };

    const int blocksAtRate1 = blocksUntilStopped(1.0f);
    const int blocksAtRate2 = blocksUntilStopped(2.0f);

    REQUIRE(blocksAtRate1 == 4);
    CHECK(blocksAtRate2 == blocksAtRate1 / 2);
}

TEST_CASE("BufferPlaybackSource scales output samples by the linear gain", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;
    constexpr float gain = 2.5f;

    auto audio = makeRampAudio(sourceLength, sampleRate);

    djapp::BufferPlaybackSource unityGain;
    unityGain.prepareToPlay(blockSize, sampleRate);
    unityGain.load(audio);
    unityGain.setGain(1.0f);
    unityGain.setPlaybackRate(1.0f);
    unityGain.setPlaying(true);
    auto unityBlock = renderBlock(unityGain, 2, blockSize);

    djapp::BufferPlaybackSource scaledGain;
    scaledGain.prepareToPlay(blockSize, sampleRate);
    scaledGain.load(audio);
    scaledGain.setGain(gain);
    scaledGain.setPlaybackRate(1.0f);
    scaledGain.setPlaying(true);
    auto scaledBlock = renderBlock(scaledGain, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(scaledBlock.getSample(0, i) == Catch::Approx(unityBlock.getSample(0, i) * gain).margin(kContentMargin));
}

TEST_CASE("BufferPlaybackSource requestSeek moves the read head", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;
    constexpr double seekSeconds = 0.5; // sample index 500

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    source.requestSeek(seekSeconds);
    // One-block latency by design: this render is the first one that reflects the seek.
    auto block = renderBlock(source, 2, blockSize);

    CHECK(block.getSample(0, 0) == Catch::Approx(500.0f).margin(kContentMargin));
    CHECK(source.getCurrentPositionSeconds() ==
          Catch::Approx(seekSeconds + (double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource loop wraps at outSeconds without exceeding it", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 20;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setLoop(djapp::LoopPoints{0.1, 0.13}); // sample range [100, 130)
    source.setPlaying(true);

    source.requestSeek(0.1);
    auto first = renderBlock(source, 2, blockSize); // applies the seek: samples 100..119
    CHECK(first.getSample(0, 0) == Catch::Approx(100.0f).margin(kContentMargin));

    float maxSample = -1.0f;
    float minSample = 1.0e9f;
    bool sawWrap = false;
    float previous = first.getSample(0, blockSize - 1);

    for (int b = 0; b < 6; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = block.getSample(0, i);
            maxSample = std::max(maxSample, value);
            minSample = std::min(minSample, value);
            if (value < previous - 1.0f)
                sawWrap = true;
            previous = value;
        }
    }

    CHECK(sawWrap);
    CHECK(maxSample < 130.0f);
    CHECK(minSample >= 99.0f);
}

TEST_CASE("BufferPlaybackSource stops and renders silence past the end of the buffer", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 100;
    constexpr int blockSize = 30;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    renderBlock(source, 2, blockSize); // samples 0..29
    renderBlock(source, 2, blockSize); // samples 30..59
    renderBlock(source, 2, blockSize); // samples 60..89
    REQUIRE(source.isPlaying());

    // 90..98 valid, then silence: linear interpolation needs a following sample, so the
    // last renderable position is numSourceFrames - 2 (index 99 has no index 100 to
    // interpolate against).
    auto finalBlock = renderBlock(source, 2, blockSize);

    for (int i = 0; i < 9; ++i)
        CHECK(finalBlock.getSample(0, i) == Catch::Approx(90.0f + (float)i).margin(kContentMargin));

    for (int i = 9; i < blockSize; ++i)
        CHECK(finalBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

    CHECK_FALSE(source.isPlaying());

    const double positionAtEnd = source.getCurrentPositionSeconds();
    CHECK(positionAtEnd >= 0.0);
    CHECK(positionAtEnd <= (double)sourceLength / sampleRate + kPositionMargin);

    auto silentBlock = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(silentBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

    CHECK_FALSE(source.isPlaying());
    CHECK(source.getCurrentPositionSeconds() == Catch::Approx(positionAtEnd).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource renders silence and holds position while paused", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;

    SECTION("before any setPlaying(true) call")
    {
        djapp::BufferPlaybackSource source;
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeRampAudio(sourceLength, sampleRate));

        auto block = renderBlock(source, 2, blockSize);

        for (int i = 0; i < blockSize; ++i)
            CHECK(block.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

        CHECK_FALSE(source.isPlaying());
        CHECK(source.getCurrentPositionSeconds() == Catch::Approx(0.0).margin(kPositionMargin));
    }

    SECTION("after setPlaying(false) mid-playback")
    {
        djapp::BufferPlaybackSource source;
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeRampAudio(sourceLength, sampleRate));
        source.setPlaybackRate(1.0f);
        source.setPlaying(true);

        renderBlock(source, 2, blockSize);
        const double positionBeforePause = source.getCurrentPositionSeconds();

        source.setPlaying(false);
        auto pausedBlock = renderBlock(source, 2, blockSize);

        for (int i = 0; i < blockSize; ++i)
            CHECK(pausedBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

        CHECK_FALSE(source.isPlaying());
        CHECK(source.getCurrentPositionSeconds() == Catch::Approx(positionBeforePause).margin(kPositionMargin));
    }
}

TEST_CASE("BufferPlaybackSource duplicates a mono source to both output channels", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 100;
    constexpr int blockSize = 30;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate, 1));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    auto block = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
    {
        CHECK(block.getSample(0, i) == Catch::Approx((float)i).margin(kContentMargin));
        CHECK(block.getSample(1, i) == Catch::Approx((float)i).margin(kContentMargin));
    }
}

TEST_CASE("BufferPlaybackSource takes the first two channels of a multichannel source",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 100;
    constexpr int blockSize = 30;
    constexpr int sourceChannels = 4;

    juce::AudioBuffer<float> buffer(sourceChannels, sourceLength);
    for (int ch = 0; ch < sourceChannels; ++ch)
        for (int i = 0; i < sourceLength; ++i)
            buffer.setSample(ch, i, (float)(ch + 1)); // constant per channel: identifies the source channel

    auto audio = std::make_shared<const djapp::LoadedAudio>(djapp::LoadedAudio{std::move(buffer), sampleRate});

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(audio);
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    auto block = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
    {
        CHECK(block.getSample(0, i) == Catch::Approx(1.0f).margin(kContentMargin));
        CHECK(block.getSample(1, i) == Catch::Approx(2.0f).margin(kContentMargin));
    }
}

TEST_CASE("BufferPlaybackSource always plays the most recently loaded buffer across repeated loads",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 20;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.setPlaybackRate(1.0f);
    source.setGain(1.0f);
    source.setPlaying(true);

    // Each load() retires the previous buffer via the generation-counter
    // scheme; interleaving a render after every load exercises that handoff
    // repeatedly rather than just once.
    for (int i = 1; i <= 6; ++i)
    {
        source.load(makeConstantAudio((float)i, sourceLength, sampleRate));
        auto block = renderBlock(source, 1, blockSize);
        CHECK(block.getSample(0, 0) == Catch::Approx((float)i).margin(kContentMargin));
        CHECK(block.getSample(0, blockSize - 1) == Catch::Approx((float)i).margin(kContentMargin));
    }
}

TEST_CASE("BufferPlaybackSource plays the latest buffer after many loads issued faster than the retire margin",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 20;
    constexpr int numLoads = 20; // several times retireMargin (4), none retired between loads

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.setPlaybackRate(1.0f);
    source.setGain(1.0f);
    source.setPlaying(true);

    // No renders between loads: retireStaleBuffers() runs every load() call
    // but nothing has aged past retireMargin generations yet, so the retire
    // list only grows here. Confirms that stress doesn't corrupt the handoff.
    for (int i = 1; i <= numLoads; ++i)
        source.load(makeConstantAudio((float)i, sourceLength, sampleRate));

    // Render past the retire margin so the accumulated retire-list entries
    // actually get reclaimed on a still-later load; playback must still be
    // reading only the last-loaded buffer throughout.
    for (int b = 0; b < 8; ++b)
    {
        auto block = renderBlock(source, 1, blockSize);
        for (int i = 0; i < blockSize; ++i)
            CHECK(block.getSample(0, i) == Catch::Approx((float)numLoads).margin(kContentMargin));
    }

    source.load(makeConstantAudio(-1.0f, sourceLength, sampleRate));
    auto finalBlock = renderBlock(source, 1, blockSize);
    CHECK(finalBlock.getSample(0, 0) == Catch::Approx(-1.0f).margin(kContentMargin));
}

TEST_CASE("BufferPlaybackSource applies a seek requested before playback ever starts", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;
    constexpr double seekSeconds = 0.3; // sample index 300

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);

    // requestSeek() while still paused: the pending-seek flag must survive
    // the later play transition rather than being lost or overwritten by the
    // seek-to-0 that load() itself already queued.
    source.requestSeek(seekSeconds);
    CHECK_FALSE(source.isPlaying());

    source.setPlaying(true);
    auto block = renderBlock(source, 2, blockSize);

    CHECK(block.getSample(0, 0) == Catch::Approx(300.0f).margin(kContentMargin));
    CHECK(source.getCurrentPositionSeconds() ==
          Catch::Approx(seekSeconds + (double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource keeps a paused seek across a rendered silent block before play",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;
    constexpr double seekSeconds = 0.3; // sample index 300

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);

    source.requestSeek(seekSeconds);
    // getNextAudioBlock() applies a pending seek unconditionally, even while
    // paused (it only gates audible output, not the position write), so a
    // paused render in between must not desynchronize the later play.
    auto pausedBlock = renderBlock(source, 2, blockSize);
    for (int i = 0; i < blockSize; ++i)
        CHECK(pausedBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

    source.setPlaying(true);
    auto block = renderBlock(source, 2, blockSize);
    CHECK(block.getSample(0, 0) == Catch::Approx(300.0f).margin(kContentMargin));
}

TEST_CASE("BufferPlaybackSource setLoop before any load stores an inactive loop without crashing",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 20;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);

    // No load() yet: messageThreadSampleRate_ is 0, so setLoop has no
    // seconds-to-samples conversion to perform. Must not crash, and must not
    // leave a garbage-valued loop active.
    source.setLoop(djapp::LoopPoints{0.05, 0.08});

    auto silentBlock = renderBlock(source, 2, blockSize);
    for (int i = 0; i < blockSize; ++i)
        CHECK(silentBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

    // Loading afterward plays straight past where the earlier loop request
    // would have wrapped, confirming it was stored inactive rather than
    // applied retroactively once a sample rate becomes available.
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    auto block = renderBlock(source, 2, blockSize);
    CHECK(block.getSample(0, blockSize - 1) == Catch::Approx((float)(blockSize - 1)).margin(kContentMargin));

    // Re-issuing setLoop after load converts against the now-known sample
    // rate and works normally.
    source.setLoop(djapp::LoopPoints{0.0, 0.01}); // sample range [0, 10)
    source.requestSeek(0.0);
    bool sawWrap = false;
    float previous = -1.0f;
    for (int b = 0; b < 4; ++b)
    {
        auto loopBlock = renderBlock(source, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = loopBlock.getSample(0, i);
            if (value < previous - 1.0f)
                sawWrap = true;
            previous = value;
        }
    }
    CHECK(sawWrap);
}

TEST_CASE("BufferPlaybackSource repeated setLoop calls always reflect the most recent loop, never a stale one",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 500;
    constexpr int blockSize = 15;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    auto maxSampleOverBlocks = [&](int numBlocks)
    {
        float maxSample = -1.0f;
        for (int b = 0; b < numBlocks; ++b)
        {
            auto block = renderBlock(source, 2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                maxSample = std::max(maxSample, block.getSample(0, i));
        }
        return maxSample;
    };

    // Exercises the double-buffered loop slot pair: A, then B, then back to
    // A, each time confirming the active render reflects only the most
    // recent setLoop() call and not whichever slot was written before it.
    source.setLoop(djapp::LoopPoints{0.0, 0.02}); // [0, 20)
    source.requestSeek(0.0);
    renderBlock(source, 2, blockSize); // applies the seek under loop A
    CHECK(maxSampleOverBlocks(6) < 20.0f);

    source.setLoop(djapp::LoopPoints{0.2, 0.25}); // [200, 250)
    source.requestSeek(0.2);
    renderBlock(source, 2, blockSize); // applies the seek under loop B
    const float maxUnderB = maxSampleOverBlocks(6);
    CHECK(maxUnderB < 250.0f);
    CHECK(maxUnderB >= 200.0f);

    source.setLoop(djapp::LoopPoints{0.0, 0.02}); // back to A
    source.requestSeek(0.0);
    renderBlock(source, 2, blockSize);
    CHECK(maxSampleOverBlocks(6) < 20.0f);
}

TEST_CASE("BufferPlaybackSource ignores a loop whose outSeconds does not exceed inSeconds",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    // Degenerate loop (out <= in): the "loop.outSamples > loop.inSamples"
    // guard should treat this as inactive rather than dividing by a
    // zero/negative-length range or spinning fmod.
    source.setLoop(djapp::LoopPoints{0.05, 0.05});
    source.setPlaying(true);

    int blocks = 0;
    while (source.isPlaying() && blocks < 20)
    {
        renderBlock(source, 2, blockSize);
        ++blocks;
    }

    // Plays straight through the ignored loop region and off the end of the
    // buffer instead of ever wrapping.
    CHECK_FALSE(source.isPlaying());
    CHECK(source.getCurrentPositionSeconds() <= (double)sourceLength / sampleRate + kPositionMargin);
}

TEST_CASE("BufferPlaybackSource renders nothing from a single-sample buffer", "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int blockSize = 10;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(1, sampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);

    // A single sample has no following sample to linearly interpolate
    // against, so the very first render immediately hits the end-of-buffer
    // stop condition without ever emitting a sample.
    auto block = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(block.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));
    CHECK_FALSE(source.isPlaying());
}

TEST_CASE("BufferPlaybackSource renders silence and stops playing after load(nullptr) mid-playback",
          "[engine][BufferPlaybackSource]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 20;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeRampAudio(sourceLength, sampleRate));
    source.setPlaybackRate(1.0f);
    source.setPlaying(true);
    renderBlock(source, 2, blockSize);

    source.load(nullptr);
    auto block = renderBlock(source, 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(block.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));

    CHECK_FALSE(source.isPlaying());
    CHECK(source.getCurrentPositionSeconds() == Catch::Approx(0.0).margin(kPositionMargin));
}

TEST_CASE("BufferPlaybackSource load() clears the loop that was active on the previously loaded track",
          "[engine][BufferPlaybackSource]")
{
    constexpr double firstSampleRate = 1000.0;
    constexpr int firstSourceLength = 300;
    // Matches deviceSampleRate_ (set once via prepareToPlay above, not
    // re-called per track) so the render increment stays 1.0 source-sample
    // per output sample; a mismatched rate here would advance position
    // faster than the test's own block math assumes and could run the
    // source dry within the render window, a confound unrelated to whether
    // load() clears the loop.
    constexpr double secondSampleRate = 1000.0;
    // Comfortably longer than the 10-block render window below can consume
    // (200 source samples at increment 1.0) so the source never self-stops
    // at end-of-buffer during the check, which would otherwise look like a
    // false wrap to the heuristic below.
    constexpr int secondSourceLength = 400;
    constexpr int blockSize = 20;

    djapp::BufferPlaybackSource source;
    source.prepareToPlay(blockSize, firstSampleRate);
    source.load(makeRampAudio(firstSourceLength, firstSampleRate));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setLoop(djapp::LoopPoints{0.05, 0.08}); // sample range [50, 80) on the first track
    source.setPlaying(true);

    source.requestSeek(0.05);
    renderBlock(source, 2, blockSize); // applies the seek: samples 50..69

    // Re-establishes a known-active-loop starting condition; the wrap
    // behavior itself is already covered by other cases in this file.
    bool sawWrapOnFirstTrack = false;
    float previousOnFirstTrack = -1.0f;
    for (int b = 0; b < 2; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = block.getSample(0, i);
            if (value < previousOnFirstTrack - 1.0f)
                sawWrapOnFirstTrack = true;
            previousOnFirstTrack = value;
        }
    }
    REQUIRE(sawWrapOnFirstTrack);

    // Long enough that the stale sample-domain range [50, 80) still falls
    // inside it: if load() failed to clear the loop, playback would wrap
    // there.
    source.load(makeRampAudio(secondSourceLength, secondSampleRate));
    source.setPlaying(true);

    bool sawWrapOnSecondTrack = false;
    float previousOnSecondTrack = -1.0f;
    double lastPosition = -1.0;
    bool positionKeptAdvancing = true;
    for (int b = 0; b < 10; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = block.getSample(0, i);
            if (value < previousOnSecondTrack - 1.0f)
                sawWrapOnSecondTrack = true;
            previousOnSecondTrack = value;
        }

        const double position = source.getCurrentPositionSeconds();
        if (position <= lastPosition)
            positionKeptAdvancing = false;
        lastPosition = position;
    }

    // 10 blocks of 20 samples = 200 samples, well past the old loop's
    // sample-domain outSamples (80) and its stale outSeconds (0.08) alike.
    CHECK_FALSE(sawWrapOnSecondTrack);
    CHECK(positionKeptAdvancing);
    CHECK(source.getCurrentPositionSeconds() > 0.08);
}
