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

    // Listeners may add or remove registrations (including their own) from within
    // their callback. Walk by token via fresh lookups each step, rather than
    // holding a map iterator across the callback, so that erase() on listeners_
    // mid-notification can never invalidate this loop's iteration state. Tokens
    // are monotonically increasing, so upper_bound naturally continues in
    // registration order and picks up newly-added listeners that sort after the
    // one currently firing.
    if (!listeners_.empty())
    {
        int token = listeners_.begin()->first;
        for (;;)
        {
            if (auto it = listeners_.find(token); it != listeners_.end())
            {
                const Listener listenerCopy = it->second;
                listenerCopy(delta, state, source);
            }

            const auto next = listeners_.upper_bound(token);
            if (next == listeners_.end())
                break;
            token = next->first;
        }
    }
}

int StateManager::addListener(Listener listener)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const int token = nextToken_++;
    listeners_.emplace(token, std::move(listener));
    return token;
}

void StateManager::removeListener(int token)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listeners_.erase(token);
}

} // namespace djapp
