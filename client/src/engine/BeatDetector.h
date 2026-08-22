#pragma once

// engine/ — BeatDetector: offline BPM + beat-phase analysis of one decoded
// track. message-thread-only, called once per track load (see EngineAdapter).

#include "model/BeatGrid.h"
#include "repository/AudioRepository.h"

namespace djapp
{

class BeatDetector
{
  public:
    virtual ~BeatDetector() = default;
    virtual BeatGrid analyze(const LoadedAudio& audio) = 0;
};

} // namespace djapp
