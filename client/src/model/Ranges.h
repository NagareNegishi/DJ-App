#pragma once

// Canonical min/max constants from docs/plan/02-protocol.md's Field reference,
// plus clamp() helpers that enforce them on already-parsed values. Range
// enforcement is deliberately separate from parsing (Serialization.h): a
// parse failure means "not valid JSON for this shape"; clamping means
// "valid shape, out-of-range value, defense-in-depth against a malicious peer".

#include "Types.h"
#include <algorithm>

namespace djapp::ranges
{

constexpr double positionSecondsMin = 0.0;
constexpr double positionSecondsMax = 86400.0;

constexpr float gainMin = 0.0f;
constexpr float gainMax = 2.0f;

constexpr float playbackRateMin = 0.5f;
constexpr float playbackRateMax = 2.0f;

constexpr float pitchOffsetSemitonesMin = -12.0f;
constexpr float pitchOffsetSemitonesMax = 12.0f;

constexpr double loopSecondsMin = 0.0;
constexpr double loopSecondsMax = 86400.0;

inline void clamp(LoopPoints& loop)
{
    loop.inSeconds = std::clamp(loop.inSeconds, loopSecondsMin, loopSecondsMax);
    loop.outSeconds = std::clamp(loop.outSeconds, loopSecondsMin, loopSecondsMax);
}

inline void clamp(PlaybackState& state)
{
    state.positionSeconds = std::clamp(state.positionSeconds, positionSecondsMin, positionSecondsMax);
    state.gain = std::clamp(state.gain, gainMin, gainMax);
    state.playbackRate = std::clamp(state.playbackRate, playbackRateMin, playbackRateMax);
    state.pitchOffsetSemitones =
        std::clamp(state.pitchOffsetSemitones, pitchOffsetSemitonesMin, pitchOffsetSemitonesMax);
    if (state.loop.has_value())
    {
        clamp(*state.loop);
        if (state.loop->inSeconds >= state.loop->outSeconds)
            state.loop = std::nullopt;
    }
}

inline void clamp(StateDelta& delta)
{
    if (delta.positionSeconds.has_value())
        delta.positionSeconds = std::clamp(*delta.positionSeconds, positionSecondsMin, positionSecondsMax);
    if (delta.gain.has_value())
        delta.gain = std::clamp(*delta.gain, gainMin, gainMax);
    if (delta.playbackRate.has_value())
        delta.playbackRate = std::clamp(*delta.playbackRate, playbackRateMin, playbackRateMax);
    if (delta.pitchOffsetSemitones.has_value())
        delta.pitchOffsetSemitones =
            std::clamp(*delta.pitchOffsetSemitones, pitchOffsetSemitonesMin, pitchOffsetSemitonesMax);
    if (delta.loop.has_value() && delta.loop->has_value())
    {
        clamp(**delta.loop);
        if ((*delta.loop)->inSeconds >= (*delta.loop)->outSeconds)
            *delta.loop = std::nullopt;
    }
}

} // namespace djapp::ranges
