#pragma once

// engine/ — playback boundary: turns PlaybackState-shaped parameter changes
// into audible output from a decoded LoadedAudio buffer. AudioEngine is the
// public interface consumed by app/state code (message-thread callers only;
// see JuceAudioEngine.h and BufferPlaybackSource.h for the audio-thread
// rendering rules of the concrete implementation).

#include "repository/AudioRepository.h"
#include <memory>

namespace djapp
{

class AudioEngine
{
  public:
    virtual ~AudioEngine() = default;
    virtual void load(std::shared_ptr<const LoadedAudio> audio) = 0; // resets position to 0, keeps gain/rate
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void setGain(float linearGain) = 0;
    virtual void setPlaybackRate(float rate) = 0;
    // A loop with outSeconds <= inSeconds is accepted but never triggers a
    // wrap (silent no-op) — not clamped, rejected, or asserted.
    virtual void setLoop(std::optional<LoopPoints> loop) = 0;
    // Holds steady at the final position once playback self-stops (see isPlaying()).
    virtual double getCurrentPosition() const = 0;
    // May become false on its own at end-of-track or on invalid/absent audio,
    // with no separate notification — callers that need to react to playback
    // ending must poll this (or compare getCurrentPosition() against track
    // duration), not expect a callback.
    virtual bool isPlaying() const = 0;
};

} // namespace djapp
