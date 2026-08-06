#include "BufferPlaybackSource.h"
#include "model/LoopWrap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <juce_events/juce_events.h>

namespace djapp
{

namespace
{
// A handful of render blocks' worth of headroom: retireStaleBuffers() only
// needs to know the audio thread has advanced past the swap at least once
// (it re-reads activeBuffer_ every call), not a fixed wall-clock delay.
constexpr uint64_t retireMargin = 4;
} // namespace

BufferPlaybackSource::BufferPlaybackSource() = default;

void BufferPlaybackSource::retireStaleBuffers()
{
    const auto currentGeneration = renderGeneration_.load(std::memory_order_acquire);
    retireList_.erase(std::remove_if(retireList_.begin(), retireList_.end(),
                                     [&](const auto& entry) { return currentGeneration - entry.first > retireMargin; }),
                      retireList_.end());
}

void BufferPlaybackSource::load(std::shared_ptr<const LoadedAudio> audio)
{
    JUCE_ASSERT_MESSAGE_THREAD

    retireStaleBuffers();

    messageThreadSampleRate_ = audio ? audio->sampleRate : 0.0;

    // Reset position to 0 before publishing the new buffer, so the audio
    // thread never renders even one block of the new buffer at a stale
    // position left over from the previous track.
    // Order matters: value first (relaxed), flag second (release) — see class header.
    pendingSeekSamples_.store(0.0, std::memory_order_relaxed);
    seekPending_.store(true, std::memory_order_release);

    // A loop's inSamples/outSamples are denominated in the previous track's
    // sample rate, which may not apply to the incoming buffer, so any active
    // loop must be cleared rather than carried over.
    const int inactiveLoopSlot = 1 - activeLoopSlot_.load(std::memory_order_relaxed);
    loopSlots_[inactiveLoopSlot] = LoopState{};
    activeLoopSlot_.store(inactiveLoopSlot, std::memory_order_release);

    const auto* rawPtr = audio.get();
    activeBuffer_.store(rawPtr, std::memory_order_release);
    const auto generationAtSwap = renderGeneration_.load(std::memory_order_acquire);

    if (messageThreadCurrentBuffer_)
        retireList_.emplace_back(generationAtSwap, std::move(messageThreadCurrentBuffer_));
    messageThreadCurrentBuffer_ = std::move(audio);
}

void BufferPlaybackSource::setPlaying(bool shouldPlay)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playing_.store(shouldPlay, std::memory_order_relaxed);
}

void BufferPlaybackSource::setGain(float linearGain)
{
    JUCE_ASSERT_MESSAGE_THREAD

    gain_.store(linearGain, std::memory_order_relaxed);
}

void BufferPlaybackSource::setPlaybackRate(float rate)
{
    JUCE_ASSERT_MESSAGE_THREAD

    rate_.store(rate, std::memory_order_relaxed);
}

void BufferPlaybackSource::setLoop(std::optional<LoopPoints> loop)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const int inactiveSlot = 1 - activeLoopSlot_.load(std::memory_order_relaxed);
    LoopState state;
    if (loop.has_value() && messageThreadSampleRate_ > 0.0)
    {
        state.active = true;
        state.inSamples = loop->inSeconds * messageThreadSampleRate_;
        state.outSamples = loop->outSeconds * messageThreadSampleRate_;
    }
    loopSlots_[inactiveSlot] = state;
    activeLoopSlot_.store(inactiveSlot, std::memory_order_release);
}

void BufferPlaybackSource::requestSeek(double seconds)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const double samples = messageThreadSampleRate_ > 0.0 ? seconds * messageThreadSampleRate_ : 0.0;
    // Order matters: value first (relaxed), flag second (release) — see class header.
    pendingSeekSamples_.store(samples, std::memory_order_relaxed);
    seekPending_.store(true, std::memory_order_release);
}

double BufferPlaybackSource::getCurrentPositionSeconds() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (messageThreadSampleRate_ <= 0.0)
        return 0.0;
    return positionSamples_.load(std::memory_order_relaxed) / messageThreadSampleRate_;
}

bool BufferPlaybackSource::isPlaying() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return playing_.load(std::memory_order_relaxed);
}

void BufferPlaybackSource::prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate)
{
    deviceSampleRate_.store(sampleRate, std::memory_order_relaxed);
}

void BufferPlaybackSource::releaseResources() {}

void BufferPlaybackSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // audio thread — see docs/plan/01-architecture.md
    renderGeneration_.fetch_add(1, std::memory_order_release);

    if (seekPending_.load(std::memory_order_acquire))
    {
        positionSamples_.store(pendingSeekSamples_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        seekPending_.store(false, std::memory_order_release);
    }

    auto* outBuffer = bufferToFill.buffer;
    const auto* audio = activeBuffer_.load(std::memory_order_acquire);
    const bool hasValidAudio = audio != nullptr && audio->buffer.getNumSamples() != 0;

    if (!hasValidAudio)
        playing_.store(false, std::memory_order_relaxed);

    if (!playing_.load(std::memory_order_relaxed) || !hasValidAudio)
    {
        outBuffer->clear(bufferToFill.startSample, bufferToFill.numSamples);
        return;
    }

    const double deviceSampleRate = deviceSampleRate_.load(std::memory_order_relaxed);
    const double sourceSampleRate = audio->sampleRate > 0.0 ? audio->sampleRate : deviceSampleRate;
    const float rate = rate_.load(std::memory_order_relaxed);
    const float gain = gain_.load(std::memory_order_relaxed);
    const double increment = deviceSampleRate > 0.0 ? static_cast<double>(rate) * (sourceSampleRate / deviceSampleRate)
                                                    : static_cast<double>(rate);

    const int loopSlot = activeLoopSlot_.load(std::memory_order_acquire);
    const LoopState loop = loopSlots_[loopSlot];

    const int numSourceChannels = audio->buffer.getNumChannels();
    const int numSourceFrames = audio->buffer.getNumSamples();
    const int outChannels = outBuffer->getNumChannels();

    // Resolved once per block (not per sample): outChannels is at most 2 per
    // the mono/stereo spec, so a fixed-size cache avoids the redundant
    // pointer-resolution cost of calling getReadPointer/setSample per sample.
    constexpr int maxOutChannels = 2;
    std::array<const float*, maxOutChannels> srcPtrs{};
    std::array<float*, maxOutChannels> dstPtrs{};
    for (int ch = 0; ch < outChannels; ++ch)
    {
        const int srcChannel = numSourceChannels == 1 ? 0 : std::min(ch, numSourceChannels - 1);
        srcPtrs[static_cast<size_t>(ch)] = audio->buffer.getReadPointer(srcChannel);
        dstPtrs[static_cast<size_t>(ch)] = outBuffer->getWritePointer(ch);
    }

    double pos = positionSamples_.load(std::memory_order_relaxed);
    bool stillPlaying = true;
    int sample = 0;

    for (; sample < bufferToFill.numSamples; ++sample)
    {
        if (loop.active)
            pos = wrapPositionWithinRange(pos, loop.inSamples, loop.outSamples);

        // -1: linear interpolation below reads src[index0] and src[index0 + 1],
        // so the last renderable head position is numSourceFrames - 2 — the
        // final sample has no successor to interpolate against.
        if (pos < 0.0 || pos >= static_cast<double>(numSourceFrames - 1))
        {
            stillPlaying = false;
            break;
        }

        const int index0 = static_cast<int>(pos);
        const int index1 = index0 + 1;
        const float frac = static_cast<float>(pos - static_cast<double>(index0));

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const float* src = srcPtrs[static_cast<size_t>(ch)];
            dstPtrs[static_cast<size_t>(ch)][bufferToFill.startSample + sample] =
                (src[index0] + frac * (src[index1] - src[index0])) * gain;
        }

        pos += increment;
    }

    if (sample < bufferToFill.numSamples)
        outBuffer->clear(bufferToFill.startSample + sample, bufferToFill.numSamples - sample);

    positionSamples_.store(pos, std::memory_order_relaxed);
    if (!stillPlaying)
        playing_.store(false, std::memory_order_relaxed);
}

} // namespace djapp
