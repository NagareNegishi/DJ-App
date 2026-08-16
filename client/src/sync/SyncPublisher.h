#pragma once

// sync/ — SyncPublisher: the StateManager listener that forwards local state
// changes to the network. It is the anti-echo-loop gate: only deltas produced
// by this client (DeltaSource::local) while connected and in the controller
// role are forwarded; remote deltas are never re-sent. Threading: constructed
// and used on the message thread only (registers a StateManager::Listener,
// which fires on that thread).

#include "SyncTransport.h"
#include "state/StateManager.h"
#include <array>
#include <juce_events/juce_events.h>
#include <optional>

namespace djapp
{

enum class Role
{
    controller,
    observer
};

// Outgoing local deltas are throttled to <=30 sendDelta calls/s per deck
// (trailing-edge coalescing): rapid deltas for the same deck within one ~33ms
// window are merged, field by field, into a single pending delta, and only the
// merged result — with each field's latest value — goes out once the window
// elapses. private juce::Timer is the mechanism that flushes pending deltas
// once their window opens, following the same idiom as PositionClock.
class SyncPublisher : private juce::Timer
{
  public:
    SyncPublisher(StateManager& stateManager, SyncTransport& transport);
    ~SyncPublisher() override;

    void setConnected(bool connected);
    void setRole(Role role);

    // Immediately sends any deck's currently pending coalesced delta, bypassing
    // the elapsed-time throttle check. Tests call this directly instead of
    // waiting on a real ~33ms Timer tick (same pattern as
    // PositionClock::emitResyncNow()).
    void flushNow();

  private:
    static constexpr int kDeckCount = 2; // matches StateManager::states_/DeckId
    static constexpr double kMinIntervalMs = 1000.0 / 30.0;

    void timerCallback() override;
    void handleLocalDelta(const StateDelta& applied);
    void maybeSend(std::size_t deckIndex);

    StateManager& stateManager_;
    SyncTransport& transport_;
    int listenerToken_ = 0;
    bool connected_ = false;
    Role role_ = Role::observer;

    std::array<std::optional<StateDelta>, kDeckCount> pending_;
    std::array<double, kDeckCount> lastSendMs_{};
    std::array<bool, kDeckCount> hasSentBefore_{};
};

} // namespace djapp
