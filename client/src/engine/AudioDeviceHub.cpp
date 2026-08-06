#include "AudioDeviceHub.h"
#include <juce_events/juce_events.h>

namespace djapp
{

AudioDeviceHub::AudioDeviceHub()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // 0 input channels, stereo out; failure (e.g. no ALSA device in a
    // container) is logged, not thrown — the app keeps running with a
    // silent device rather than crashing.
    const auto error = deviceManager_.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("AudioDeviceHub: no audio device available: " + error);

    sourcePlayer_.setSource(&mixer_);
    deviceManager_.addAudioCallback(&sourcePlayer_);
}

AudioDeviceHub::~AudioDeviceHub()
{
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(nullptr);
}

void AudioDeviceHub::addSource(juce::AudioSource& source)
{
    JUCE_ASSERT_MESSAGE_THREAD

    mixer_.addInputSource(&source, false);
}

void AudioDeviceHub::removeSource(juce::AudioSource& source)
{
    JUCE_ASSERT_MESSAGE_THREAD

    mixer_.removeInputSource(&source);
}

juce::AudioDeviceManager& AudioDeviceHub::deviceManager()
{
    JUCE_ASSERT_MESSAGE_THREAD

    return deviceManager_;
}

} // namespace djapp
