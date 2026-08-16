#include "PositionClock.h"

namespace djapp
{

PositionClock::PositionClock(StateManager& stateManager, AudioEngine& engine, DeckId deck)
    : stateManager_(stateManager), engine_(engine), deck_(deck)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listenerToken_ = stateManager_.addListener(
        [this](const StateDelta& applied, const PlaybackState&, DeltaSource)
        {
            if (applied.deck != deck_)
                return;

            if (applied.playing.has_value())
                updateTimerState();
        });
}

PositionClock::~PositionClock()
{
    stateManager_.removeListener(listenerToken_);
}

void PositionClock::setRole(Role role)
{
    JUCE_ASSERT_MESSAGE_THREAD

    role_ = role;
    updateTimerState();
}

void PositionClock::emitResyncNow()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (role_ != Role::controller || !stateManager_.getState(deck_).playing)
        return;

    StateDelta delta;
    delta.deck = deck_;
    delta.positionSeconds = engine_.getCurrentPosition();
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void PositionClock::updateTimerState()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (role_ == Role::controller && stateManager_.getState(deck_).playing)
        startTimer(5000);
    else
        stopTimer();
}

void PositionClock::timerCallback()
{
    emitResyncNow();
}

} // namespace djapp
