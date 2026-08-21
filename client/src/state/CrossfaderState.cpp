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
    const float targetPosition = clamped;

    // A listener that calls setPosition again re-entrantly runs its own full
    // walk against the new value via TokenListenerList; once that happens
    // position_ no longer matches targetPosition, so shouldStop tells this
    // walk to stop rather than resuming and re-notifying listeners the
    // nested walk already covered.
    listeners_.notify([&](const Listener& listenerCopy) { listenerCopy(position_); },
                      [&] { return position_ != targetPosition; });
}

float CrossfaderState::getPosition() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    return position_;
}

int CrossfaderState::addListener(Listener listener)
{
    JUCE_ASSERT_MESSAGE_THREAD

    return listeners_.addListener(std::move(listener));
}

void CrossfaderState::removeListener(int token)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listeners_.removeListener(token);
}

} // namespace djapp
