#pragma once

// BufferPlaybackSource — the pure DSP core of engine/: renders one deck's
// decoded LoadedAudio into a device-rate output block. Owns no threads of
// its own; it is driven by whichever juce::AudioSource host calls it
// (offline in tests, or via AudioDeviceHub on the real device).
//
// Threading (binding, docs/plan/01-architecture.md): getNextAudioBlock() runs
// on the real-time audio thread — no locks, no allocation, no juce::String,
// no logging, no JUCE message-thread calls, ever, in that method or anything
// it calls. All tunable parameters are std::atomic fields written by the
// message thread; the buffer itself is handed over via an atomic raw-pointer
// publish plus a generation-counter epoch reclaim scheme (see load() and
// retireStaleBuffers()) rather than a shared_ptr the audio thread would need
// to copy/destroy. positionSamples_ is written only by the audio thread;
// seek() requests go through a pending-value/pending-flag pair so the audio
// thread applies them at a safe point instead of tearing a concurrent write.
// Store order for that pair is an invariant: pendingSeekSamples_ must be
// written before seekPending_ is set, so the audio thread's acquire-load of
// the flag can never observe it raised before the value it guards is visible.
// Every public method other than the juce::AudioSource overrides is
// message-thread-only (JUCE_ASSERT_MESSAGE_THREAD) and is the sole way
// JuceAudioEngine mutates this object; all mutable state below is private.
//
// prepareToPlay()/releaseResources() are called directly by JUCE's audio
// device layer around device start/stop (not by JuceAudioEngine), and are
// not concurrent with getNextAudioBlock() by JUCE's own contract.

#include "TimeStretcher.h"
#include "repository/AudioRepository.h"
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace djapp
{

class BufferPlaybackSource : public juce::AudioSource
{
  public:
    BufferPlaybackSource();
    explicit BufferPlaybackSource(std::unique_ptr<TimeStretcher> stretcher);
    ~BufferPlaybackSource() override = default;

    void load(std::shared_ptr<const LoadedAudio> audio); // resets position to 0
    void setPlaying(bool shouldPlay);
    void setGain(float linearGain);
    void setPlaybackRate(float rate);
    void setPitchOffsetSemitones(float semitones);
    void setLoop(std::optional<LoopPoints> loop); // see AudioEngine.h: outSeconds <= inSeconds is a silent no-op
    // Unlike loop points, repeat carries no per-track units, so load() intentionally leaves
    // this untouched (see BufferPlaybackSource.cpp's load()).
    void setRepeat(bool repeat);
    void requestSeek(double seconds);

    double getCurrentPositionSeconds() const;
    bool isPlaying() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

  private:
    // Plain struct deliberately without atomics inside it: the pair is only
    // ever mutated as a whole (write the inactive slot, then flip the index)
    // so the audio thread can never observe a torn combination of the two.
    struct LoopState
    {
        bool active = false;
        double inSamples = 0, outSamples = 0;
    };

    void retireStaleBuffers();

    // --- audio-thread-read, message-thread-written parameters ---
    std::atomic<bool> playing_{false};
    std::atomic<float> gain_{1.0f};
    std::atomic<float> rate_{1.0f};
    std::atomic<bool> repeat_{true};
    std::atomic<double> deviceSampleRate_{0.0};
    std::atomic<float> pitchOffsetSemitones_{0.0f};

    // --- position: audio thread is the sole writer; message thread only
    // requests a seek via the pending pair below ---
    std::atomic<double> positionSamples_{0.0};
    std::atomic<double> pendingSeekSamples_{0.0};
    std::atomic<bool> seekPending_{false};

    // --- stretcher-reset trigger, deliberately separate from seekPending_
    // above: seekPending_ gates only the position write (every corrective
    // write - drift-correction resync, local play(), a future sync-button
    // nudge - goes through it), while forceStretcherReset_ only fires for a
    // real discontinuity (new track, or a seek far enough to not be a small
    // corrective write). See load()/requestSeek() in the .cpp. ---
    std::atomic<bool> forceStretcherReset_{false};

    // --- TimeStretcher: constructed once for this object's lifetime, never
    // swapped - no generation-counter handoff needed for it, unlike
    // activeBuffer_ below. All stretcher_ calls are confined to
    // prepareToPlay()/releaseResources()/getNextAudioBlock() (see class header). ---
    std::unique_ptr<TimeStretcher> stretcher_;

    // Fixed 2-channel scratch buffers, sized only inside prepareToPlay() (never
    // on the audio thread). pullScratch_ holds resampled, device-rate-equivalent
    // frames pulled from the source, ready for the stretcher's input;
    // stretchOutScratch_ holds the stretcher's output before the final
    // gain+copy into the real destination buffer.
    std::vector<float> pullScratch_[2];
    std::vector<float> stretchOutScratch_[2];

    // Audio-thread-only fractional carry for the tempo-ratio frame count, so
    // the long-run average of pulled frames per output frame is exactly
    // `rate`, not a per-block rounding of it. Reset to 0 whenever the
    // stretcher resets.
    double pullAccumulator_ = 0.0;

    // Audio-thread-only count of output frames emitted (+= numOutput every
    // block) since the last reset; used only for the latency ramp in
    // getNextAudioBlock()'s position step. Deliberately not named after
    // "position" - it counts blocks rendered, not source position.
    int64_t producedOutputFrames_ = 0;

    // Audio-thread-cached copy of stretcher_->outputLatencyFrames(), refreshed
    // once per reset rather than called every block.
    int outputLatencyFrames_ = 0;

    // Audio-thread-only, output-frame domain: bounds the end-of-track tail so
    // a track doesn't truncate its last fraction of a second, without ever
    // running indefinitely, and without forcing a stop if the pull recovers
    // real audio before the drain expires (repeat/loop toggled mid-drain).
    int drainFramesRemaining_ = 0;

    // --- loop points: double-buffered slot pair, see LoopState comment ---
    LoopState loopSlots_[2];
    std::atomic<int> activeLoopSlot_{0};

    // --- buffer handoff: audio thread only ever touches the raw pointer and
    // the generation counter, never the shared_ptr itself ---
    std::atomic<const LoadedAudio*> activeBuffer_{nullptr};
    std::atomic<uint64_t> renderGeneration_{0};

    // --- message-thread-only bookkeeping ---
    double messageThreadSampleRate_ = 0.0; // sample rate of the buffer last passed to load()
    std::shared_ptr<const LoadedAudio> messageThreadCurrentBuffer_;
    std::vector<std::pair<uint64_t, std::shared_ptr<const LoadedAudio>>> retireList_;
};

} // namespace djapp
