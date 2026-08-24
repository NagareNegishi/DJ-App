#include "BufferPlaybackSource.h"
#include "IdentityTimeStretcher.h"
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

BufferPlaybackSource::BufferPlaybackSource() : BufferPlaybackSource(std::make_unique<IdentityTimeStretcher>()) {}

BufferPlaybackSource::BufferPlaybackSource(std::unique_ptr<TimeStretcher> stretcher) : stretcher_(std::move(stretcher))
{
}

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

    // A new track is always a real discontinuity regardless of numeric distance from the
    // previous position (it's also a new buffer, so there's nothing meaningful to measure a
    // "distance" against) - unconditional, unlike requestSeek()'s distance-gated version below.
    forceStretcherReset_.store(true, std::memory_order_relaxed);

    messageThreadSampleRate_ = audio ? audio->sampleRate : 0.0;

    // Reset position to 0 before publishing the new buffer, so the audio
    // thread never renders even one block of the new buffer at a stale
    // position left over from the previous track.
    // Order matters: value first (relaxed), flag second (release) — see class header.
    // repeat_ is deliberately NOT reset here: unlike the loop slot below (denominated in
    // the previous track's sample rate, must be cleared) and the seek-to-0 reset above,
    // repeat is a track-independent user preference/synced setting that must survive a
    // track change unchanged.
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

void BufferPlaybackSource::setPitchOffsetSemitones(float semitones)
{
    JUCE_ASSERT_MESSAGE_THREAD

    pitchOffsetSemitones_.store(semitones, std::memory_order_relaxed);
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

        if (messageThreadCurrentBuffer_)
        {
            const double lastRenderableFrame =
                static_cast<double>(messageThreadCurrentBuffer_->buffer.getNumSamples() - 1);
            state.inSamples = std::min(state.inSamples, lastRenderableFrame);
            state.outSamples = std::min(state.outSamples, lastRenderableFrame);
        }
    }
    loopSlots_[inactiveSlot] = state;
    activeLoopSlot_.store(inactiveSlot, std::memory_order_release);
}

void BufferPlaybackSource::setRepeat(bool repeat)
{
    JUCE_ASSERT_MESSAGE_THREAD

    repeat_.store(repeat, std::memory_order_relaxed);
}

void BufferPlaybackSource::requestSeek(double seconds)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const double samples = messageThreadSampleRate_ > 0.0 ? seconds * messageThreadSampleRate_ : 0.0;

    // A small corrective write (the 5 s drift-correction resync PositionClock emits while
    // playing, or the positionSeconds StateManager injects on a local play()) must not reset
    // the stretcher - only a real discontinuity should. The threshold must also clear the sync
    // button's own phase nudge (BeatSync.h), whose maximum magnitude is half a beat interval -
    // at a low but realistic BPM (50-60), that's up to ~0.6s, above the old 200ms threshold (a
    // bug: it was documented as excluding the sync nudge but didn't). 1.0s comfortably clears
    // that worst case while staying far below any real user-initiated seek.
    // forceStretcherReset_ is deliberately its own flag, independent of seekPending_ below
    // (which continues to gate only the position write) - see class header / TimeStretcher.h.
    if (std::abs(samples - positionSamples_.load(std::memory_order_relaxed)) > messageThreadSampleRate_ * 1.0)
        forceStretcherReset_.store(true, std::memory_order_relaxed);

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

void BufferPlaybackSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    deviceSampleRate_.store(sampleRate, std::memory_order_relaxed);

    // This class renders exactly 2 internal channels, always (AudioDeviceHub hardcodes
    // stereo output - see class header); the pull stage below resamples source audio to
    // this same device rate before it ever reaches the stretcher, so the stretcher genuinely
    // only ever sees device-rate frames.
    stretcher_->prepare(2, sampleRate);

    // Generous fixed capacity that will never need to grow on the audio thread later: covers
    // up to playbackRateMax (2.0, see model/Ranges.h) plus headroom. resize(), not reserve() -
    // the pull/process stages index into these by position, not push_back.
    const int capacityFrames = std::max(samplesPerBlockExpected, 512) * 4 + 4096;
    for (int ch = 0; ch < 2; ++ch)
    {
        pullScratch_[ch].resize(static_cast<size_t>(capacityFrames));
        stretchOutScratch_[ch].resize(static_cast<size_t>(capacityFrames));
    }
}

void BufferPlaybackSource::releaseResources() {}

void BufferPlaybackSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // audio thread — see docs/plan/01-architecture.md
    renderGeneration_.fetch_add(1, std::memory_order_release);

    // 1. Reset detection: forceStretcherReset_ is the actual stretcher-reset trigger, kept
    // deliberately independent of seekPending_ below (which continues to gate only the
    // position write) - see class header and TimeStretcher.h for why the two must never be
    // merged back together.
    const bool needsStretchReset = forceStretcherReset_.exchange(false, std::memory_order_acq_rel);
    if (needsStretchReset)
    {
        stretcher_->reset();
        outputLatencyFrames_ = stretcher_->outputLatencyFrames();
        pullAccumulator_ = 0.0;
        producedOutputFrames_ = 0;
        lastLaggedSourceFrames_ = 0.0;
        drainFramesRemaining_ = 0; // cancel any in-progress drain from a prior track/seek
    }

    if (seekPending_.load(std::memory_order_acquire))
    {
        positionSamples_.store(pendingSeekSamples_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        seekPending_.store(false, std::memory_order_release);
    }

    // 2. Early-out — unchanged.
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

    // 3. Capacity guard: makes "the destination is always fully written by the time this
    // function returns" a real invariant rather than something derived from scratch-buffer
    // sizing the reader can't see.
    const int numOutput = bufferToFill.numSamples;
    if (numOutput == 0)
        return; // nothing to render; do not perturb pullAccumulator_/pos/the stretcher for a no-op call
    if (pullScratch_[0].size() == 0 || static_cast<size_t>(numOutput) > stretchOutScratch_[0].size())
    {
        outBuffer->clear(bufferToFill.startSample, bufferToFill.numSamples);
        return;
    }

    // 4. Read parameters.
    const double deviceSampleRate = deviceSampleRate_.load(std::memory_order_relaxed);
    const double sourceSampleRate = audio->sampleRate > 0.0 ? audio->sampleRate : deviceSampleRate;
    const float rate = rate_.load(std::memory_order_relaxed);
    const float gain = gain_.load(std::memory_order_relaxed);
    const bool repeat = repeat_.load(std::memory_order_relaxed);
    const float pitchOffsetSemitones = pitchOffsetSemitones_.load(std::memory_order_relaxed);
    stretcher_->setPitchSemitones(pitchOffsetSemitones);

    const int loopSlot = activeLoopSlot_.load(std::memory_order_acquire);
    const LoopState loop = loopSlots_[loopSlot];

    const int numSourceChannels = audio->buffer.getNumChannels();
    const int numSourceFrames = audio->buffer.getNumSamples();
    const int outChannels = outBuffer->getNumChannels();

    // This class always renders exactly 2 internal channels regardless of outChannels (see
    // prepareToPlay); resolved once per block, not per sample.
    std::array<const float*, 2> srcPtrs{};
    for (int ch = 0; ch < 2; ++ch)
    {
        const int srcChannel = numSourceChannels == 1 ? 0 : std::min(ch, numSourceChannels - 1);
        srcPtrs[static_cast<size_t>(ch)] = audio->buffer.getReadPointer(srcChannel);
    }

    // 5. Pull stage — sample-rate conversion only, no tempo change here (tempo is entirely
    // decided by pullFrames below, fed to the stretcher as its numInput/numOutput ratio in
    // step 6). srcToDeviceRatio is NOT multiplied by rate anywhere in this stage.
    const double srcToDeviceRatio = deviceSampleRate > 0.0 ? sourceSampleRate / deviceSampleRate : 1.0;

    // Fractional carry so the long-run average pulled-frames-per-output-frame is exactly
    // `rate`, not a per-block rounding of it (a bare round() would integrate a constant bias
    // into audible tempo drift over a track's length).
    pullAccumulator_ += static_cast<double>(numOutput) * static_cast<double>(rate);
    int pullFrames = std::max(1, static_cast<int>(std::floor(pullAccumulator_)));
    pullAccumulator_ -= pullFrames;
    // Defensive only - should never bind given prepareToPlay's sizing; clamp silently, never
    // allocate, never log.
    pullFrames = std::min(pullFrames, static_cast<int>(pullScratch_[0].size()));

    // Reconstruct the true raw pull position from the latency-compensated value step 9 last
    // published: this exactly undoes the subtraction step 9 applied last block, using the same
    // cached value that was actually subtracted then - not a fresh recomputation with this
    // block's rate/srcToDeviceRatio, which could differ from last block's if rate_ changed
    // between blocks and would then under- or over-shoot the true compensation. Without this,
    // the pull stage would silently re-seed from the already-lag-compensated position every
    // block and the raw read head would drift further behind real audio each block, compounding
    // without bound whenever outputLatencyFrames_ is nonzero (any real stretcher).
    double pos = positionSamples_.load(std::memory_order_relaxed) + lastLaggedSourceFrames_;

    // -1: linear interpolation below reads src[index0] and src[index0 + 1], so the last
    // renderable head position is numSourceFrames - 2 ... i.e. this is the exclusive upper
    // bound both the stop test and the repeat wrap must agree on.
    const double lastRenderableFrame = static_cast<double>(numSourceFrames - 1);

    bool pullRanDry = false;
    int i = 0;
    for (; i < pullFrames; ++i)
    {
        if (loop.active)
            pos = wrapPositionWithinRange(pos, loop.inSamples, loop.outSamples);

        // Whole-track repeat only ever overrides the forward (end-of-track) boundary, never a
        // negative position, and only once the region loop above (if any) has NOT already brought
        // pos back under the boundary this same iteration. wrapPositionWithinRange is a no-op
        // whenever its range is degenerate (rangeEnd <= rangeStart, e.g. a 0- or 1-frame buffer),
        // so a corrupt/tiny buffer falls straight through to the unchanged stop test below instead
        // of ever looping forever here — this loop is still bounded by `pullFrames` regardless.
        if (repeat && pos >= lastRenderableFrame)
            pos = wrapPositionWithinRange(pos, 0.0, lastRenderableFrame);

        if (pos < 0.0 || pos >= lastRenderableFrame)
        {
            pullRanDry = true;
            break;
        }

        const int index0 = static_cast<int>(pos);
        const int index1 = index0 + 1;
        const float frac = static_cast<float>(pos - static_cast<double>(index0));

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = srcPtrs[static_cast<size_t>(ch)];
            pullScratch_[ch][static_cast<size_t>(i)] = src[index0] + frac * (src[index1] - src[index0]);
        }

        pos += srcToDeviceRatio;
    }

    if (pullRanDry)
    {
        // First block to run dry since the last reset - a later block that's already draining
        // doesn't re-arm. +1 avoids a zero-length drain when latency is 0 (IdentityTimeStretcher),
        // still stopping promptly in that case.
        if (drainFramesRemaining_ == 0)
            drainFramesRemaining_ = outputLatencyFrames_ + 1;

        // Legitimate zero-valued samples flowing through the stretcher normally (not a
        // zero-count, no-op call), letting an STFT-based stretcher's own windowing produce a
        // natural fade from whatever real content it still has buffered, rather than an
        // abrupt cut.
        for (int j = i; j < pullFrames; ++j)
        {
            pullScratch_[0][static_cast<size_t>(j)] = 0.0f;
            pullScratch_[1][static_cast<size_t>(j)] = 0.0f;
        }
    }

    // 6. Stretch.
    const float* pullPtrs[2] = {pullScratch_[0].data(), pullScratch_[1].data()};
    float* stretchOutPtrs[2] = {stretchOutScratch_[0].data(), stretchOutScratch_[1].data()};
    stretcher_->process(pullPtrs, pullFrames, stretchOutPtrs, numOutput);
    producedOutputFrames_ += numOutput;

    // 7. Gain + write to the real destination. Writes destination channels 0 and 1 only; if
    // outChannels == 1, only stretchOutScratch_[0] is written (no averaging/mixing).
    const int channelsToWrite = std::min(outChannels, 2);
    for (int ch = 0; ch < channelsToWrite; ++ch)
    {
        float* dst = outBuffer->getWritePointer(ch, bufferToFill.startSample);
        const float* stretched = stretchOutScratch_[static_cast<size_t>(ch)].data();
        for (int o = 0; o < numOutput; ++o)
            dst[o] = stretched[o] * gain;
    }
    // Explicitly clear any destination channel at index >= 2 - nothing else in this pipeline
    // touches them.
    for (int ch = 2; ch < outChannels; ++ch)
        outBuffer->clear(ch, bufferToFill.startSample, numOutput);

    // 8. Drain bound and self-stop.
    if (drainFramesRemaining_ > 0)
    {
        if (pullRanDry)
        {
            // Output-frame domain, matching what producedOutputFrames_/outputLatencyFrames_
            // are denominated in - not pullFrames (the same input/output mixing step 9's
            // comment below explains, applied to the end-of-track tail instead of position).
            drainFramesRemaining_ = std::max(0, drainFramesRemaining_ - numOutput);
            if (drainFramesRemaining_ == 0)
                playing_.store(false, std::memory_order_relaxed); // only place playing_ clears for end-of-track
        }
        else
        {
            // A mid-drain block whose pull unexpectedly found real audio again (e.g.
            // repeat/loop toggled on while draining) - cancel the drain, playback continues.
            drainFramesRemaining_ = 0;
        }
    }

    // 9. Position, latency-compensated: positionSamples_ must report the audible position, not
    // the raw pull position, or it runs permanently ahead of what's actually coming out of the
    // speakers. laggedOutputFrames is how much of the stretcher's warm-up backlog (in
    // output-frame terms) hasn't been "paid off" yet since the last reset, 0 once
    // producedOutputFrames_ >= outputLatencyFrames_; `* rate` converts that output-frame lag
    // into the equivalent input (pulled)-frame lag, and `* srcToDeviceRatio` converts
    // pulled-frame lag into source-buffer-frame lag, matching the two conversions `pos` itself
    // is downstream of. This is a known approximation, not exact: outputLatencyFrames_ itself
    // conflates the stretcher's separate input-domain and output-domain latencies into one
    // number (a TimeStretcher interface limitation, out of this unit's scope to change), so the
    // ramp's shape is a reasonable estimate rather than a bit-exact model.
    const double laggedOutputFrames =
        std::min(static_cast<double>(producedOutputFrames_), static_cast<double>(outputLatencyFrames_));
    const double laggedSourceFrames = laggedOutputFrames * static_cast<double>(rate) * srcToDeviceRatio;
    lastLaggedSourceFrames_ = laggedSourceFrames;
    positionSamples_.store(pos - laggedSourceFrames, std::memory_order_relaxed);
}

} // namespace djapp
