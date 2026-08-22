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

    // May allocate/reserve. Must be called from a context provably not
    // concurrent with any in-flight process() call - the owning render driver
    // decides where that point is (never assume it's literally "the message
    // thread" - only that it's serialized against process()). sampleRate is
    // this stretcher's one input/output sample rate; the caller must ensure
    // every process() call's frames are already at this rate before reaching
    // this class - it never performs sample-rate conversion itself.
    virtual void prepare(int numChannels, double sampleRate) = 0;
    // Audio-thread-safe once prepare() has run (no locks, no allocation) - same
    // safety class as process()/setPitchSemitones(). The caller must still never
    // call this concurrently with an in-flight process() call from a DIFFERENT
    // thread; the safe pattern is to have whichever single thread drives
    // rendering call reset() itself (gated by its own pending-state check)
    // rather than have a separate thread call it directly.
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
