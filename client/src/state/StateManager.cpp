#include "StateManager.h"
#include "model/Ranges.h"
#include <juce_events/juce_events.h>

namespace djapp
{

namespace
{
std::size_t indexOf(DeckId deck)
{
    return deck == DeckId::A ? 0 : 1;
}
} // namespace

const PlaybackState& StateManager::getState(DeckId deck) const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return states_[indexOf(deck)];
}

void StateManager::applyDelta(StateDelta delta, DeltaSource source)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (delta.empty())
        return;

    ranges::clamp(delta);

    auto& state = states_[indexOf(delta.deck)];

    // A delta carrying playing:true must carry position (01-architecture.md);
    // local senders may omit it, so inject the deck's current stored position
    // before merging so listeners see it in the delta itself.
    if (source == DeltaSource::local && delta.playing.has_value() && *delta.playing &&
        !delta.positionSeconds.has_value())
        delta.positionSeconds = state.positionSeconds;

    if (delta.trackId.has_value())
        state.trackId = *delta.trackId;
    if (delta.playing.has_value())
        state.playing = *delta.playing;
    if (delta.positionSeconds.has_value())
        state.positionSeconds = *delta.positionSeconds;
    if (delta.gain.has_value())
        state.gain = *delta.gain;
    if (delta.playbackRate.has_value())
        state.playbackRate = *delta.playbackRate;
    if (delta.pitchOffsetSemitones.has_value())
        state.pitchOffsetSemitones = *delta.pitchOffsetSemitones;
    if (delta.loop.has_value())
        state.loop = *delta.loop; // outer engaged: inner nullopt clears the loop, inner value sets it
    if (delta.repeat.has_value())
        state.repeat = *delta.repeat;

    // Listeners may add or remove registrations (including their own) from within
    // their callback; TokenListenerList's walk re-looks-up the current token via
    // fresh lookups each step, so that mid-notification changes can never invalidate
    // this iteration. Tokens are monotonically increasing, so the walk naturally
    // continues in registration order and picks up newly-added listeners that sort
    // after the one currently firing.
    listeners_.notify([&](const Listener& listenerCopy) { listenerCopy(delta, state, source); }, [] { return false; });
}

int StateManager::addListener(Listener listener)
{
    JUCE_ASSERT_MESSAGE_THREAD

    return listeners_.addListener(std::move(listener));
}

void StateManager::removeListener(int token)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listeners_.removeListener(token);
}

} // namespace djapp
