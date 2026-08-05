#pragma once

// repository/ — abstract audio catalogue/decode boundary. All methods are
// message-thread-only for the prototype (synchronous load — acceptable per
// docs/plan/04-client.md, "loading off the message thread is not required
// for prototype"; docs/plan/01-architecture.md's threading table lists
// "repository loads (async)" for the eventual design, which this prototype
// deliberately doesn't implement yet). No method may throw out to its caller
// for an expected failure — return nullptr/nullopt/empty instead.

#include "model/Types.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace djapp
{

struct LoadedAudio
{
    juce::AudioBuffer<float> buffer; // whole track in memory (DJ-standard: instant seek)
    double sampleRate = 0;
};

class AudioRepository
{
public:
    virtual ~AudioRepository() = default;
    virtual std::vector<TrackMetadata> listAvailableTracks() = 0;
    virtual std::optional<TrackMetadata> getTrackMetadata(const juce::String& id) = 0;
    virtual std::shared_ptr<const LoadedAudio> getAudioBuffer(const juce::String& id) = 0; // nullptr on failure
};

} // namespace djapp
