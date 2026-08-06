#pragma once

// engine/ — EngineAdapter drives one deck's AudioEngine from StateManager
// notifications, resolving StateDelta::trackId through AudioRepository (the
// one place a network-safe trackId becomes a local file load). Deck-scoped,
// not source-scoped: acts identically whether the applied delta was local or
// remote. Also writes back the one direction StateManager notifications can't
// cover: while playing, it polls for the engine having stopped itself
// (end-of-track or a failed load) and corrects StateManager's stale
// `playing` flag. Succession rule: this adapter writes back only facts the
// engine alone can observe and StateManager cannot derive any other way;
// this poll is expected to be absorbed into or coordinated with
// state/PositionClock once that lands at M7 — not a general license for the
// adapter to write more into state. Threading: message-thread-only, same as
// the StateManager notification it's always called from.

#include "AudioEngine.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"
#include "state/StateManager.h"
#include <juce_events/juce_events.h>

namespace djapp
{

class EngineAdapter : private juce::Timer
{
  public:
    EngineAdapter(StateManager& stateManager, DeckId deck, AudioEngine& engine, AudioRepository& repository);
    ~EngineAdapter() override;

    // Idempotent; safe to call at any time on the message thread, whether or
    // not there's anything to correct.
    void checkForSelfStop();

  private:
    void handleDelta(const StateDelta& applied, const PlaybackState& newState, DeltaSource source);
    void timerCallback() override; // calls checkForSelfStop() at low frequency; see there

    StateManager& stateManager_;
    DeckId deck_;
    AudioEngine& engine_;
    AudioRepository& repository_;
    int listenerToken_;
};

} // namespace djapp
