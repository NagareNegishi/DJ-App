#pragma once
// engine/ — IdentityTimeStretcher: naive linear-resample passthrough, no pitch
// shift. Reproduces BufferPlaybackSource's pre-M10 resample behavior exactly
// (phase-continuous fractional read pointer across process() calls) so a
// caller with no real stretcher injected keeps behaving exactly like today's
// engine. See TimeStretcher.h for the full threading contract: process() is
// intended to be called from the audio thread once wired in a later unit —
// this unit never calls it from any thread itself.

#include "TimeStretcher.h"

namespace djapp
{

class IdentityTimeStretcher : public TimeStretcher
{
  public:
    void prepare(int numChannels, double sampleRate) override;
    void reset() override;

    void setPitchSemitones(float semitones) override; // no-op: identity never shifts pitch
    void process(const float* const* inChannels, int numInput, float* const* outChannels, int numOutput) override;

    int outputLatencyFrames() const override; // always 0: no internal buffering

  private:
    // Channel count last passed to prepare() (message-thread-written there,
    // audio-thread-read in process(), matching TimeStretcher.h's contract
    // that inChannels/outChannels each carry exactly this many entries).
    int numChannels_ = 0;

    // Phase-continuous fractional read pointer, relative to the most recently
    // consumed process() call's own inChannels. Audio-thread-mutated only
    // inside process(); reset to 0.0 only by reset() (message-thread-only).
    double readPos_ = 0.0;
};

} // namespace djapp
