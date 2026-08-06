#include "engine/JuceAudioEngine.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <memory>
#include <optional>

// JuceAudioEngine is a pure forwarding layer over BufferPlaybackSource
// (see JuceAudioEngine.cpp): these tests drive it only through the AudioEngine
// interface and confirm the forwarding itself is correct, rather than
// re-deriving BufferPlaybackSourceTest's DSP-level coverage.

namespace
{

std::shared_ptr<const djapp::LoadedAudio> makeRampAudio(int numSamples, double sampleRate, int numChannels = 1)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample(ch, i, (float)i);

    return std::make_shared<const djapp::LoadedAudio>(djapp::LoadedAudio{std::move(buffer), sampleRate});
}

juce::AudioBuffer<float> renderBlock(juce::AudioSource& source, int numChannels, int numSamples)
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

TEST_CASE("JuceAudioEngine reports no playback and zero position before any load", "[engine][JuceAudioEngine]")
{
    djapp::JuceAudioEngine engine;

    CHECK_FALSE(engine.isPlaying());
    CHECK(engine.getCurrentPosition() == Catch::Approx(0.0).margin(kPositionMargin));
}

TEST_CASE("JuceAudioEngine::load forwards to the underlying source and renders its content",
          "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;

    djapp::JuceAudioEngine engine;
    engine.source().prepareToPlay(blockSize, sampleRate);
    engine.load(makeRampAudio(sourceLength, sampleRate));
    engine.play();

    auto block = renderBlock(engine.source(), 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(block.getSample(0, i) == Catch::Approx((float)i).margin(kContentMargin));
    CHECK(engine.getCurrentPosition() == Catch::Approx((double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("JuceAudioEngine::play and pause forward to isPlaying and toggle rendered silence",
          "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;

    djapp::JuceAudioEngine engine;
    engine.source().prepareToPlay(blockSize, sampleRate);
    engine.load(makeRampAudio(sourceLength, sampleRate));

    CHECK_FALSE(engine.isPlaying());

    engine.play();
    CHECK(engine.isPlaying());
    auto playingBlock = renderBlock(engine.source(), 2, blockSize);
    CHECK(playingBlock.getSample(0, 1) == Catch::Approx(1.0f).margin(kContentMargin));

    engine.pause();
    CHECK_FALSE(engine.isPlaying());
    auto pausedBlock = renderBlock(engine.source(), 2, blockSize);
    for (int i = 0; i < blockSize; ++i)
        CHECK(pausedBlock.getSample(0, i) == Catch::Approx(0.0f).margin(kSilenceMargin));
}

TEST_CASE("JuceAudioEngine::seek forwards to requestSeek and takes effect on the next render",
          "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;
    constexpr double seekSeconds = 0.4; // sample index 400

    djapp::JuceAudioEngine engine;
    engine.source().prepareToPlay(blockSize, sampleRate);
    engine.load(makeRampAudio(sourceLength, sampleRate));
    engine.play();

    engine.seek(seekSeconds);
    auto block = renderBlock(engine.source(), 2, blockSize);

    CHECK(block.getSample(0, 0) == Catch::Approx(400.0f).margin(kContentMargin));
    CHECK(engine.getCurrentPosition() ==
          Catch::Approx(seekSeconds + (double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("JuceAudioEngine::setGain forwards and scales rendered output", "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 200;
    constexpr int blockSize = 40;
    constexpr float gain = 3.0f;

    auto audio = makeRampAudio(sourceLength, sampleRate);

    djapp::JuceAudioEngine unityEngine;
    unityEngine.source().prepareToPlay(blockSize, sampleRate);
    unityEngine.load(audio);
    unityEngine.play();
    auto unityBlock = renderBlock(unityEngine.source(), 2, blockSize);

    djapp::JuceAudioEngine gainEngine;
    gainEngine.source().prepareToPlay(blockSize, sampleRate);
    gainEngine.load(audio);
    gainEngine.setGain(gain);
    gainEngine.play();
    auto gainBlock = renderBlock(gainEngine.source(), 2, blockSize);

    for (int i = 0; i < blockSize; ++i)
        CHECK(gainBlock.getSample(0, i) == Catch::Approx(unityBlock.getSample(0, i) * gain).margin(kContentMargin));
}

TEST_CASE("JuceAudioEngine::setPlaybackRate forwards and changes the position advance rate",
          "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 1000;
    constexpr int blockSize = 50;

    djapp::JuceAudioEngine engine;
    engine.source().prepareToPlay(blockSize, sampleRate);
    engine.load(makeRampAudio(sourceLength, sampleRate));
    engine.setPlaybackRate(2.0f);
    engine.play();

    renderBlock(engine.source(), 2, blockSize);

    CHECK(engine.getCurrentPosition() == Catch::Approx(2.0 * (double)blockSize / sampleRate).margin(kPositionMargin));
}

TEST_CASE("JuceAudioEngine::setLoop forwards and causes the render to wrap", "[engine][JuceAudioEngine]")
{
    constexpr double sampleRate = 1000.0;
    constexpr int sourceLength = 300;
    constexpr int blockSize = 20;

    djapp::JuceAudioEngine engine;
    engine.source().prepareToPlay(blockSize, sampleRate);
    engine.load(makeRampAudio(sourceLength, sampleRate));
    engine.setLoop(djapp::LoopPoints{0.1, 0.13}); // sample range [100, 130)
    engine.seek(0.1);
    engine.play();

    renderBlock(engine.source(), 2, blockSize); // applies the seek

    float previous = -1.0f;
    bool sawWrap = false;
    for (int b = 0; b < 6; ++b)
    {
        auto block = renderBlock(engine.source(), 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = block.getSample(0, i);
            if (value < previous - 1.0f)
                sawWrap = true;
            previous = value;
        }
    }

    CHECK(sawWrap);

    // Clearing the loop (nullopt) forwards through too: subsequent playback
    // runs off the end of the buffer instead of continuing to wrap.
    engine.setLoop(std::nullopt);
    engine.seek(0.29); // sample 290, ten samples from the end
    int blocks = 0;
    while (engine.isPlaying() && blocks < 20)
    {
        renderBlock(engine.source(), 2, blockSize);
        ++blocks;
    }
    CHECK_FALSE(engine.isPlaying());
}

TEST_CASE("JuceAudioEngine::source returns the same underlying AudioSource on every call", "[engine][JuceAudioEngine]")
{
    djapp::JuceAudioEngine engine;
    CHECK(&engine.source() == &engine.source());
}
