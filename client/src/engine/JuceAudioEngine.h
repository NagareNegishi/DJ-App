#pragma once

// JuceAudioEngine — AudioEngine implementation backed by a single
// BufferPlaybackSource. Owns one deck's worth of playback state; device
// wiring (the audio callback thread itself) lives in AudioDeviceHub, one per
// app, which the composition root plugs this engine's source() into.
//
// Threading: every method here is message-thread-only (JUCE_ASSERT_MESSAGE_THREAD)
// and does nothing but translate a call into a BufferPlaybackSource setter —
// the setters themselves carry the real-time-safety guarantees.

#include "AudioEngine.h"
#include "BufferPlaybackSource.h"

namespace djapp
{

class JuceAudioEngine : public AudioEngine
{
  public:
    JuceAudioEngine();
    ~JuceAudioEngine() override = default;

    void load(std::shared_ptr<const LoadedAudio> audio) override;
    void play() override;
    void pause() override;
    void seek(double seconds) override;
    void setGain(float linearGain) override;
    void setPlaybackRate(float rate) override;
    void setLoop(std::optional<LoopPoints> loop) override;
    double getCurrentPosition() const override;
    bool isPlaying() const override;

    // The AudioSource the composition root plugs into AudioDeviceHub's mixer;
    // not part of the AudioEngine interface (JUCE type, engine-internal wiring).
    juce::AudioSource& source();

  private:
    BufferPlaybackSource playbackSource_;
};

} // namespace djapp
