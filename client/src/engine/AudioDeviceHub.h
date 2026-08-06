#pragma once

// AudioDeviceHub — the one-per-app owner of the real audio device callback.
// Wraps juce::AudioDeviceManager (stereo out, default device) + an
// AudioSourcePlayer feeding a MixerAudioSource that decks plug their
// JuceAudioEngine::source() into. GUI-target-only: not part of dj-app-core,
// since it pulls in juce_audio_devices and has nothing offline-testable of
// its own (BufferPlaybackSource carries the testable logic).
//
// Threading: every public method here is message-thread-only
// (JUCE_ASSERT_MESSAGE_THREAD); the resulting audio callback thread is
// entirely internal to JUCE's AudioDeviceManager/AudioSourcePlayer and never
// touches this class directly. Device initialisation failure (e.g. no ALSA
// device in a container) is reported to the log, never thrown or crashed on
// — the hub stays alive and safe to add/remove sources from either way.

#include <juce_audio_devices/juce_audio_devices.h>

namespace djapp
{

class AudioDeviceHub
{
  public:
    AudioDeviceHub();
    ~AudioDeviceHub();

    // Neither owns nor deletes the source; caller (composition root) keeps
    // each deck's JuceAudioEngine alive for at least as long as it's added.
    void addSource(juce::AudioSource& source);
    void removeSource(juce::AudioSource& source);

    juce::AudioDeviceManager& deviceManager();

  private:
    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer sourcePlayer_;
    juce::MixerAudioSource mixer_;
};

} // namespace djapp
