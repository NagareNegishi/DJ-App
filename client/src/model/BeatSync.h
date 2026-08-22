#pragma once

// model/ — computeBeatSync: the sync-button correction (BPM match + phase
// nudge) from two decks' BeatGrids. Pure math, no JUCE/engine/repository
// dependency, same shape as model/LoopWrap.h and model/CrossfaderCurve.h, so
// it's testable in isolation from engine/EngineAdapter, its one production
// consumer (docs/plan/10-beatsync-design.md, "Sync-button semantics").

#include "BeatGrid.h"
#include "Ranges.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace djapp
{

struct BeatSyncResult
{
    float playbackRate = 1.0f;
    double positionSeconds = 0.0;
};

// Computes the correction to apply to "this" deck so it matches "other".
// thisCurrentPositionSeconds/otherCurrentPositionSeconds are each deck's live
// playhead position at the moment of the button press (not StateManager's
// possibly-stale positionSeconds while playing). thisDurationSeconds is this
// deck's track length, used only to keep the phase nudge from landing outside
// the track (seeking past end-of-track would silently self-stop the engine,
// see AudioEngine.h's isPlaying() contract) — pass 0 if unknown, which
// disables that clamp and falls back to the protocol-wide position range.
// Returns std::nullopt when sync isn't possible (either deck's BPM detection
// failed, i.e. bpm <= 0) — the caller must not push a StateDelta in that case,
// not "push one with unset/unchanged fields": nullopt is the only failure
// signal, there is no in-band value for it (an in-band playbackRate == 1.0 is
// also a legitimate successful-sync value, so it cannot double as a failure
// flag).
inline std::optional<BeatSyncResult> computeBeatSync(const BeatGrid& thisGrid, double thisCurrentPositionSeconds,
                                                      const BeatGrid& otherGrid, double otherCurrentPositionSeconds,
                                                      double thisDurationSeconds = 0.0)
{
    if (thisGrid.bpm <= 0.0 || otherGrid.bpm <= 0.0)
        return std::nullopt;

    BeatSyncResult result;

    const float ratio = static_cast<float>(otherGrid.bpm / thisGrid.bpm);
    result.playbackRate = std::clamp(ratio, ranges::playbackRateMin, ranges::playbackRateMax);

    // Both decks' phases wrapped into THIS deck's post-match beat interval -
    // that's the interval "this" deck will actually be advancing at once the
    // rate change above lands, so it's the correct common ruler for "in
    // phase". Computed from BOTH decks' positions and BOTH decks'
    // firstBeatSeconds: this is what actually aligns "this" deck's beats to
    // "other" deck's beats, rather than merely quantizing "this" deck to its
    // own grid (which would leave the two decks' beats unrelated).
    const double beatIntervalSeconds = 60.0 / thisGrid.bpm;
    auto wrappedPhase = [&](double position, double firstBeatSeconds)
    {
        const double phase = std::fmod(position - firstBeatSeconds, beatIntervalSeconds);
        return phase < 0.0 ? phase + beatIntervalSeconds : phase;
    };
    const double thisPhase = wrappedPhase(thisCurrentPositionSeconds, thisGrid.firstBeatSeconds);
    const double otherPhase = wrappedPhase(otherCurrentPositionSeconds, otherGrid.firstBeatSeconds);

    // Shortest signed correction that makes thisPhase == otherPhase, wrapped
    // to within half a beat interval either way (never a full re-seek).
    double delta = std::fmod(otherPhase - thisPhase, beatIntervalSeconds);
    if (delta > beatIntervalSeconds / 2.0)
        delta -= beatIntervalSeconds;
    else if (delta < -beatIntervalSeconds / 2.0)
        delta += beatIntervalSeconds;

    double targetPosition = thisCurrentPositionSeconds + delta;
    if (thisDurationSeconds > 0.0)
        targetPosition = std::clamp(targetPosition, 0.0, thisDurationSeconds);
    result.positionSeconds = std::clamp(targetPosition, ranges::positionSecondsMin, ranges::positionSecondsMax);

    return result;
}

} // namespace djapp
