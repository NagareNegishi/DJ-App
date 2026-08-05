#pragma once

// engine/ — EngineAdapter drives one deck's AudioEngine from StateManager
// notifications, resolving StateDelta::trackId through AudioRepository (the
// one place a network-safe trackId becomes a local file load). Deck-scoped,
// not source-scoped: acts identically whether the applied delta was local or
// remote. Threading: message-thread-only, same as the StateManager
// notification it's always called from.

#include "AudioEngine.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"
#include "state/StateManager.h"

namespace djapp
{

class EngineAdapter
{
  public:
    EngineAdapter(StateManager& stateManager, DeckId deck, AudioEngine& engine, AudioRepository& repository);
    ~EngineAdapter();

  private:
    void handleDelta(const StateDelta& applied, const PlaybackState& newState, DeltaSource source);

    StateManager& stateManager_;
    DeckId deck_;
    AudioEngine& engine_;
    AudioRepository& repository_;
    int listenerToken_;
};

} // namespace djapp
