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
// possibly-stale positionSeconds while playing). otherPlaybackRate is the
// other deck's current playbackRate, so the tempo match targets what's
// actually audible on that deck right now (native BPM x its current rate),
// not just its file's native detected BPM - defaults to 1.0f for callers that
// don't care. thisDurationSeconds is this deck's track length, used only to
// keep the phase nudge from landing outside the track (seeking past
// end-of-track would silently self-stop the engine, see AudioEngine.h's
// isPlaying() contract) — pass 0 if unknown, which disables that check. If
// the computed nudge would land outside [0, thisDurationSeconds], the nudge
// is dropped and position is left unchanged (the tempo-match correction
// still applies) rather than clamped to the boundary, since clamping to
// end-of-track would immediately re-trigger the engine's self-stop.
// Returns std::nullopt when sync isn't possible (either deck's BPM detection
// failed, i.e. bpm <= 0) — the caller must not push a StateDelta in that case,
// not "push one with unset/unchanged fields": nullopt is the only failure
// signal, there is no in-band value for it (an in-band playbackRate == 1.0 is
// also a legitimate successful-sync value, so it cannot double as a failure
// flag).
inline std::optional<BeatSyncResult> computeBeatSync(const BeatGrid& thisGrid, double thisCurrentPositionSeconds,
                                                      const BeatGrid& otherGrid, double otherCurrentPositionSeconds,
                                                      float otherPlaybackRate = 1.0f,
                                                      double thisDurationSeconds = 0.0)
{
    if (thisGrid.bpm <= 0.0 || otherGrid.bpm <= 0.0)
        return std::nullopt;

    BeatSyncResult result;

    // Step 1: tempo match, now accounting for the reference deck's own current rate -
    // "effectiveOtherBpm" is what's actually audible on the other deck right now, not
    // just its file's native detected BPM.
    const double effectiveOtherBpm = otherGrid.bpm * static_cast<double>(otherPlaybackRate);
    const float ratio = static_cast<float>(effectiveOtherBpm / thisGrid.bpm);
    result.playbackRate = std::clamp(ratio, ranges::playbackRateMin, ranges::playbackRateMax);

    // Step 2: phase nudge. Each deck's phase is wrapped using ITS OWN beat interval -
    // this is the bug being fixed: the old code wrapped both decks' phase using
    // thisGrid's interval, which is only correct when the two BPMs happen to be equal.
    const double thisBeatInterval = 60.0 / thisGrid.bpm;
    const double otherBeatInterval = 60.0 / otherGrid.bpm;
    auto wrappedPhase = [](double position, double firstBeatSeconds, double interval)
    {
        const double phase = std::fmod(position - firstBeatSeconds, interval);
        return phase < 0.0 ? phase + interval : phase;
    };
    const double thisPhase = wrappedPhase(thisCurrentPositionSeconds, thisGrid.firstBeatSeconds, thisBeatInterval);
    const double otherPhase = wrappedPhase(otherCurrentPositionSeconds, otherGrid.firstBeatSeconds, otherBeatInterval);

    // Scale otherPhase into thisBeatInterval's domain. This ratio is algebraically exact
    // and, notably, does NOT need otherPlaybackRate - it cancels out completely once
    // Step 1 above already accounts for it. Do not multiply/divide this line by
    // otherPlaybackRate too, that would double-count it. The result is already
    // guaranteed to land in [0, thisBeatInterval) by construction, but fmod-wrap
    // defensively for float precision at the boundary.
    double targetThisPhase = std::fmod((otherGrid.bpm / thisGrid.bpm) * otherPhase, thisBeatInterval);
    if (targetThisPhase < 0.0)
        targetThisPhase += thisBeatInterval;

    // Shortest signed correction, wrapped to within half a beat interval either way -
    // same shape as before.
    double delta = targetThisPhase - thisPhase;
    if (delta > thisBeatInterval / 2.0)
        delta -= thisBeatInterval;
    else if (delta < -thisBeatInterval / 2.0)
        delta += thisBeatInterval;

    double targetPosition = thisCurrentPositionSeconds + delta;

    // Range fix: if the nudge would leave the track, do NOT clamp to the boundary -
    // that silently discards the phase alignment just computed, and at the
    // end-of-track boundary specifically, clamping lands on the last renderable frame
    // and immediately re-triggers the engine's own end-of-track self-stop. Fall back to
    // leaving position unchanged (no nudge) instead - the tempo-match correction above
    // still applies either way.
    if (thisDurationSeconds > 0.0 && (targetPosition < 0.0 || targetPosition > thisDurationSeconds))
        targetPosition = thisCurrentPositionSeconds;

    result.positionSeconds = std::clamp(targetPosition, ranges::positionSecondsMin, ranges::positionSecondsMax);

    return result;
}

} // namespace djapp
