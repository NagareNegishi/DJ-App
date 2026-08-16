#pragma once

// state/ — PositionClock: while this client is the controller and a deck is
// playing, periodically reads the true position off this client's own engine
// and re-emits it as a local delta, correcting the drift every other client's
// free-running extrapolation accumulates between deltas.

#include "engine/AudioEngine.h"
#include "model/Types.h"
#include "state/StateManager.h"
#include "sync/SyncPublisher.h" // reuse Role — do not redeclare it
#include <juce_events/juce_events.h>

namespace djapp
{

class PositionClock : private juce::Timer
{
  public:
    PositionClock(StateManager& stateManager, AudioEngine& engine, DeckId deck);
    ~PositionClock() override;

    void setRole(Role role);

    // Idempotent; safe to call on the message thread at any time, whether or not
    // there's anything to send. Applies a local positionSeconds-only delta for this
    // deck if (and only if) role == controller and the deck is currently playing.
    // The real 5 s cadence comes from timerCallback(); tests call this directly
    // instead of waiting on a real juce::Timer (same pattern as
    // EngineAdapter::checkForSelfStop()).
    void emitResyncNow();

  private:
    void timerCallback() override; // calls emitResyncNow()
    void updateTimerState();       // start/stop the 5 s Timer based on role_ && the
                                   // deck's current `playing` flag; called from the
                                   // StateManager listener and from setRole()

    StateManager& stateManager_;
    AudioEngine& engine_;
    DeckId deck_;
    Role role_ = Role::observer;
    int listenerToken_;
};

} // namespace djapp
