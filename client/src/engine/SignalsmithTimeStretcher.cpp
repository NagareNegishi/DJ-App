#include "SignalsmithTimeStretcher.h"

// The one translation unit allowed to see a Signalsmith header (see this
// class's header comment / the PRIVATE include-dir scoping in CMakeLists.txt).
//
// Note: at the pinned commit (44c8f865af9da8c29cc4a70a2d5a3ec83639c711, tag
// v1.1.0), signalsmith-stretch.h pulls in only its own bundled "dsp/" headers
// (a self-contained, older "signalsmith-dsp" library) - it does not include or
// depend on Signalsmith Linear at all. So there is no separate Linear::reserve()
// call to make here: SignalsmithStretch::configure() (called by presetDefault()
// below) already does all its own internal std::vector::reserve()/resize() calls
// up front, sized from numChannels/sampleRate, which is what keeps process() and
// setTransposeSemitones() allocation-free afterward. See this unit's final report
// for the corresponding docs/plan/10-beatsync-design.md discrepancy this surfaces.
#include "signalsmith-stretch.h"

namespace djapp
{

struct SignalsmithTimeStretcher::Impl
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
};

SignalsmithTimeStretcher::SignalsmithTimeStretcher() : impl_(std::make_unique<Impl>()) {}

SignalsmithTimeStretcher::~SignalsmithTimeStretcher() = default;

void SignalsmithTimeStretcher::prepare(int numChannels, double sampleRate)
{
    impl_->stretch.presetDefault(numChannels, static_cast<float>(sampleRate));
    // Without a priming seek() call, the delay before the first genuine output
    // sample is the combined input + output latency (see the vendored
    // library's README, "Latency" section), not outputLatency() alone.
    outputLatencyFrames_ = impl_->stretch.inputLatency() + impl_->stretch.outputLatency();
}

void SignalsmithTimeStretcher::reset()
{
    impl_->stretch.reset();
}

void SignalsmithTimeStretcher::setPitchSemitones(float semitones)
{
    impl_->stretch.setTransposeSemitones(semitones);
}

void SignalsmithTimeStretcher::process(const float* const* inChannels, int numInput, float* const* outChannels,
                                       int numOutput)
{
    if (numInput == 0 || numOutput == 0)
        return; // defined no-op (see TimeStretcher.h) - handled here rather than relied on
                // from the library, whose own process() is not documented against this case.

    impl_->stretch.process(inChannels, numInput, outChannels, numOutput);
}

int SignalsmithTimeStretcher::outputLatencyFrames() const
{
    return outputLatencyFrames_;
}

} // namespace djapp
