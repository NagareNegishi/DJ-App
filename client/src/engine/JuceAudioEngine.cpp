#include "JuceAudioEngine.h"
#include <juce_events/juce_events.h>

namespace djapp
{

JuceAudioEngine::JuceAudioEngine() = default;

void JuceAudioEngine::load(std::shared_ptr<const LoadedAudio> audio)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.load(std::move(audio));
}

void JuceAudioEngine::play()
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setPlaying(true);
}

void JuceAudioEngine::pause()
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setPlaying(false);
}

void JuceAudioEngine::seek(double seconds)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.requestSeek(seconds);
}

void JuceAudioEngine::setGain(float linearGain)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setGain(linearGain);
}

void JuceAudioEngine::setPlaybackRate(float rate)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setPlaybackRate(rate);
}

void JuceAudioEngine::setLoop(std::optional<LoopPoints> loop)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setLoop(loop);
}

void JuceAudioEngine::setRepeat(bool repeat)
{
    JUCE_ASSERT_MESSAGE_THREAD

    playbackSource_.setRepeat(repeat);
}

double JuceAudioEngine::getCurrentPosition() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return playbackSource_.getCurrentPositionSeconds();
}

bool JuceAudioEngine::isPlaying() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return playbackSource_.isPlaying();
}

juce::AudioSource& JuceAudioEngine::source()
{
    JUCE_ASSERT_MESSAGE_THREAD

    return playbackSource_;
}

} // namespace djapp
