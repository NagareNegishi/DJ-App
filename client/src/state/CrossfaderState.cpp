#include "CrossfaderState.h"
#include <algorithm>
#include <juce_events/juce_events.h>

namespace djapp
{

void CrossfaderState::setPosition(float position)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const float clamped = std::clamp(position, 0.0f, 1.0f);
    if (clamped == position_)
        return;

    position_ = clamped;

    // Listeners may add or remove registrations (including their own) from within
    // their callback; walk by token via fresh lookups each step, matching
    // StateManager::applyDelta's notification discipline.
    if (!listeners_.empty())
    {
        int token = listeners_.begin()->first;
        for (;;)
        {
            if (auto it = listeners_.find(token); it != listeners_.end())
            {
                const Listener listenerCopy = it->second;
                listenerCopy(position_);
            }

            const auto next = listeners_.upper_bound(token);
            if (next == listeners_.end())
                break;
            token = next->first;
        }
    }
}

float CrossfaderState::getPosition() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return position_;
}

int CrossfaderState::addListener(Listener listener)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const int token = nextToken_++;
    listeners_.emplace(token, std::move(listener));
    return token;
}

void CrossfaderState::removeListener(int token)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listeners_.erase(token);
}

} // namespace djapp
