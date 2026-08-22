#pragma once

// model/ — BeatGrid: one track's detected tempo and beat phase. Pure data,
// no dependency on engine/ or repository/ — see model/BeatSync.h, the other
// consumer that must not pull engine/ in transitively through this type.

namespace djapp
{

struct BeatGrid
{
    double bpm = 0;              // 0 = detection failed / silence / too short
    double firstBeatSeconds = 0; // position of the first detected beat
};

} // namespace djapp
