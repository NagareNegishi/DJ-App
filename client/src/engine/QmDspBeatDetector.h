#pragma once

// engine/ — QmDspBeatDetector: real offline analysis backed by vendored
// qm-dsp (DetectionFunction + TempoTrackV2), the same class pair Mixxx uses
// for this job (docs/plan/10-beatsync-design.md, "BeatDetector"). qm-dsp's
// own headers are confined to QmDspBeatDetector.h/.cpp — this header only
// needs BeatDetector's own djapp types, so it never pulls a qm-dsp header in.

#include "BeatDetector.h"

namespace djapp
{

class QmDspBeatDetector : public BeatDetector
{
  public:
    BeatGrid analyze(const LoadedAudio& audio) override;
};

} // namespace djapp
