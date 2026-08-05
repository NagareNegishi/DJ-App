#pragma once

// state/ — canonical local playback state, one per deck. Every state change,
// whether triggered by local UI action or a remote delta, flows through
// StateManager::applyDelta, which clamps, merges, then notifies listeners
// (EngineAdapter, UI, SyncPublisher) with the applied delta and its source.
// Threading: message-thread-only; JUCE_ASSERT_MESSAGE_THREAD in every public method.

#include "model/Types.h"
#include <array>
#include <functional>
#include <map>

namespace djapp
{

enum class DeltaSource
{
    local,
    remote
};

class StateManager
{
  public:
    using Listener = std::function<void(const StateDelta& applied, const PlaybackState& newState, DeltaSource)>;

    const PlaybackState& getState(DeckId deck) const;

    // Drops empty deltas untouched; otherwise clamps, merges present fields
    // into the deck's stored state, then notifies listeners in registration order.
    void applyDelta(StateDelta delta, DeltaSource source);

    // Token starts at 0, increases monotonically, never reused.
    int addListener(Listener listener);
    void removeListener(int token);

  private:
    std::array<PlaybackState, 2> states_; // indexed by DeckId (A, B), both default-constructed
    std::map<int, Listener> listeners_;   // keys increase monotonically -> registration order preserved
    int nextToken_ = 0;
};

} // namespace djapp
