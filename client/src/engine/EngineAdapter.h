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
// adapter to write more into state. Optionally composes state.gain with the
// local-only CrossfaderState (see attachCrossfader) before pushing to the
// engine; the crossfader itself never touches StateManager or the sync layer.
// Threading: message-thread-only throughout — handleDelta from the StateManager
// notification it's always called from, checkForSelfStop/timerCallback from the
// Timer callback, independent of any notification.

#include "AudioEngine.h"
#include "engine/BeatDetector.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"
#include "state/CrossfaderState.h"
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

    // Optional post-construction attachment (not a constructor parameter: that
    // would force every existing call site, including tests, to change). Once
    // attached, this deck's effective gain becomes state.gain times this deck's
    // side of the crossfader's equal-power curve; immediately pushes the
    // effective gain for the crossfader's current position so the engine agrees
    // with the on-screen fader before the user ever touches it.
    void attachCrossfader(CrossfaderState& crossfader);

    // Optional post-construction attachment (same reasoning as attachCrossfader:
    // a constructor parameter would force every existing call site, including
    // tests, to change). Once attached, a successful trackId load analyzes the
    // resolved buffer and caches the result; currentBeatGrid() returns
    // BeatGrid{} (bpm == 0, "detection failed") until this has been called and
    // a track has successfully loaded since.
    void attachBeatDetector(BeatDetector& detector);

    // The current cached BeatGrid for whatever track is currently loaded on
    // this deck (BeatGrid{} if no BeatDetector is attached, no track has
    // loaded, or the last load failed/was cleared).
    const BeatGrid& currentBeatGrid() const { return cachedBeatGrid_; }

  private:
    void handleDelta(const StateDelta& applied, const PlaybackState& newState, DeltaSource source);
    void timerCallback() override; // calls checkForSelfStop() at low frequency; see there

    // Single place state.gain and the crossfader multiplier get combined and
    // pushed to engine_.setGain(...); called both from handleDelta's gain branch
    // and from the crossfader listener, so the two never compute the product
    // independently.
    void pushEffectiveGain(float gain);

    StateManager& stateManager_;
    DeckId deck_;
    AudioEngine& engine_;
    AudioRepository& repository_;
    int listenerToken_;

    CrossfaderState* crossfader_ = nullptr;
    int crossfaderListenerToken_ = 0;

    BeatDetector* beatDetector_ = nullptr;
    BeatGrid cachedBeatGrid_;
    // Identity-only: compared against the freshly resolved track's trackId to
    // skip re-analysis when reselecting an already-loaded track. Keyed on
    // trackId identity (a juce::String, owning its own data), not
    // buffer-pointer identity - a stable identity that doesn't depend on
    // AudioRepository's caching behavior continuing to return the same
    // pointer for a given track.
    juce::String lastAnalyzedTrackId_;
};

} // namespace djapp
