#pragma once

// model/ — position-wrap arithmetic shared by the audio-thread loop
// enforcement (engine/BufferPlaybackSource, sample-domain) and the UI's
// display/capture logic (ui/DeckComponent, second-domain), so the two units
// stay a single source of truth instead of two copies kept in sync by
// comment alone.

#include <cmath>

namespace djapp
{

// fmod (not a hard reset to rangeStart) deliberately preserves the
// fractional overshoot past rangeEnd, so interpolation stays smooth across
// the wrap seam.
inline double wrapPositionWithinRange(double position, double rangeStart, double rangeEnd)
{
    if (rangeEnd > rangeStart && position >= rangeEnd)
    {
        const double length = rangeEnd - rangeStart;
        return rangeStart + std::fmod(position - rangeStart, length);
    }
    return position;
}

} // namespace djapp
