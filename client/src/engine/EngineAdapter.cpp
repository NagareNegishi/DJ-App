#include "EngineAdapter.h"
#include <juce_events/juce_events.h>

namespace djapp
{

EngineAdapter::EngineAdapter(StateManager& stateManager, DeckId deck, AudioEngine& engine, AudioRepository& repository)
    : stateManager_(stateManager), deck_(deck), engine_(engine), repository_(repository)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listenerToken_ = stateManager_.addListener([this](const StateDelta& applied, const PlaybackState& newState,
                                                      DeltaSource source) { handleDelta(applied, newState, source); });
}

EngineAdapter::~EngineAdapter()
{
    stateManager_.removeListener(listenerToken_);
}

void EngineAdapter::handleDelta(const StateDelta& applied, const PlaybackState& /*newState*/, DeltaSource /*source*/)
{
    if (applied.deck != deck_)
        return;

    if (applied.trackId.has_value())
    {
        if (applied.trackId->isEmpty())
        {
            juce::Logger::writeToLog("EngineAdapter: track cleared on deck " + toString(deck_) +
                                     "; unload not supported, ignoring");
        }
        else
        {
            const auto buffer = repository_.getAudioBuffer(*applied.trackId);
            if (buffer == nullptr)
                juce::Logger::writeToLog("EngineAdapter: failed to load audio for track \"" + *applied.trackId + "\"");
            else
                engine_.load(buffer);
        }
    }

    if (applied.positionSeconds.has_value())
        engine_.seek(*applied.positionSeconds);

    if (applied.gain.has_value())
        engine_.setGain(*applied.gain);

    if (applied.playbackRate.has_value())
        engine_.setPlaybackRate(*applied.playbackRate);

    if (applied.loop.has_value())
        engine_.setLoop(*applied.loop);

    if (applied.playing.has_value())
        *applied.playing ? engine_.play() : engine_.pause();
}

} // namespace djapp
