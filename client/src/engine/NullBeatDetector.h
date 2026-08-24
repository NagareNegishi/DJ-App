#pragma once

// engine/ — NullBeatDetector: returns a caller-supplied BeatGrid instead of
// running real analysis, for wiring tests that exercise the sync-button
// happy path without needing real detection. The default BeatGrid{} is the
// "detection failed" case; pass a real grid to exercise the success path,
// since every downstream consumer (model/BeatSync.h) treats bpm <= 0 as
// "detection failed, do nothing" and a fixed always-BeatGrid{} double could
// never exercise anything past that check.

#include "BeatDetector.h"

namespace djapp
{

class NullBeatDetector : public BeatDetector
{
  public:
    explicit NullBeatDetector(BeatGrid gridToReturn = BeatGrid{}) : gridToReturn_(gridToReturn) {}
    BeatGrid analyze(const LoadedAudio&) override { return gridToReturn_; }

  private:
    BeatGrid gridToReturn_;
};

} // namespace djapp
