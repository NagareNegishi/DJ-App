#pragma once

// model/ — the crossfader's equal-power gain curve, pulled out as its own pure
// header so it's testable in isolation from engine/EngineAdapter, its one
// production consumer. Kept out of engine/ rather than inlined there for the
// same reason model/Ranges.h and model/ControlGating.h live here: pure math,
// no JUCE/engine dependency, covered by a plain Catch2 suite.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace djapp
{

struct CrossfaderGains
{
    float gainA = 1.0f;
    float gainB = 1.0f;
};

// position: 0.0 = full deck A, 1.0 = full deck B, 0.5 = center. Real equal-power curve
// (matches how every physical DJ mixer's crossfader behaves): gainA=cos(p*pi/2),
// gainB=sin(p*pi/2), so center is ~0.707 on both sides (~-3dB), not full volume on
// both — that dip is intentional, not a bug, and keeps perceived loudness roughly
// constant while sweeping across two decks playing at once.
inline CrossfaderGains equalPowerCrossfade(float position)
{
    position = std::clamp(position, 0.0f, 1.0f);
    const float angle = position * std::numbers::pi_v<float> / 2.0f;
    return CrossfaderGains{std::cos(angle), std::sin(angle)};
}

} // namespace djapp
