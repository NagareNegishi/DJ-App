#pragma once
// engine/ — TimeStretcher: changes playback tempo and/or pitch independently.
// See docs/plan/10-beatsync-design.md for the interface rationale, and this
// file's implementing spec for the full threading/precondition contract.
namespace djapp
{
class TimeStretcher
{
  public:
    virtual ~TimeStretcher() = default;

    // Message-thread-only; may allocate/reserve. sampleRate is this
    // stretcher's one input/output sample rate (see spec: callers own any
    // source/device rate conversion, this class never sees two rates).
    virtual void prepare(int numChannels, double sampleRate) = 0;
    // Message-thread-only; flushes internal state (e.g. on seek).
    virtual void reset() = 0;

    // Audio-thread-safe once prepare() has run (no locks, no allocation).
    // [-12, 12] semitones, independent of tempo.
    virtual void setPitchSemitones(float semitones) = 0;
    // Audio-thread-safe once prepare() has run (no locks, no allocation).
    // Preconditions: inChannels/outChannels each have prepare()'s numChannels
    // entries; the two arrays never alias; numInput==0 or numOutput==0 is a
    // defined no-op.
    virtual void process(const float* const* inChannels, int numInput,
                          float* const* outChannels, int numOutput) = 0;

    // Frames of output delay this implementation introduces before its first
    // real output sample is available (0 for a stretcher with no internal
    // buffering, e.g. IdentityTimeStretcher). Message-thread-only; valid only
    // after prepare(). Callers use this to pre-roll at load() rather than
    // producing an audible startup gap — see the design doc's "Startup
    // latency" note.
    virtual int outputLatencyFrames() const = 0;
};
} // namespace djapp
