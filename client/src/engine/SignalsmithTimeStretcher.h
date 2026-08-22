#pragma once
// engine/ — SignalsmithTimeStretcher: real time/pitch stretch backed by the
// vendored Signalsmith Stretch library (docs/plan/10-beatsync-design.md).
// Pimpl: the header holds only a pointer to the (incomplete) Impl type plus
// the interface overrides, so no Signalsmith header is visible outside this
// class's own .cpp — matches sync/WebSocketTransport.h's pimpl containment
// of IXWebSocket, and is what justifies the vendored include dirs being
// PRIVATE (not PUBLIC) on dj-app-core in CMakeLists.txt.
//
// Threading: see TimeStretcher.h for the full contract. process() and
// setPitchSemitones() are intended to be called from the audio thread once
// wired in a later unit; this unit never calls them from any thread itself.

#include "TimeStretcher.h"
#include <memory>

namespace djapp
{

class SignalsmithTimeStretcher : public TimeStretcher
{
  public:
    SignalsmithTimeStretcher();
    ~SignalsmithTimeStretcher() override;

    void prepare(int numChannels, double sampleRate) override;
    void reset() override;

    void setPitchSemitones(float semitones) override;
    void process(const float* const* inChannels, int numInput, float* const* outChannels, int numOutput) override;

    int outputLatencyFrames() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Cached in prepare() (see TimeStretcher.h: outputLatencyFrames() is
    // valid only after prepare() has run).
    int outputLatencyFrames_ = 0;
};

} // namespace djapp
