#include "IdentityTimeStretcher.h"
#include <algorithm>
#include <cmath>

namespace djapp
{

void IdentityTimeStretcher::prepare(int numChannels, double /*sampleRate*/)
{
    // Channel count only: the per-call step is derived purely from
    // numInput/numOutput, not from anything rate-dependent fixed here.
    numChannels_ = numChannels;
}

void IdentityTimeStretcher::reset()
{
    readPos_ = 0.0;
}

void IdentityTimeStretcher::setPitchSemitones(float /*semitones*/)
{
    // Documented no-op: identity never pitch-shifts.
}

void IdentityTimeStretcher::process(const float* const* inChannels, int numInput, float* const* outChannels,
                                    int numOutput)
{
    if (numInput == 0 || numOutput == 0)
        return; // defined no-op: writes nothing, leaves readPos_ unchanged

    const double step = double(numInput) / double(numOutput);
    const double posBefore = readPos_;

    for (int o = 0; o < numOutput; ++o)
    {
        const double pos = posBefore + double(o) * step;
        const double floorPos = std::floor(pos);
        const int index0 = std::clamp(static_cast<int>(floorPos), 0, numInput - 1);
        const int index1 = std::min(index0 + 1, numInput - 1);
        const float frac = static_cast<float>(pos - floorPos);

        for (int ch = 0; ch < numChannels_; ++ch)
        {
            const float* in = inChannels[ch];
            outChannels[ch][o] = in[index0] + frac * (in[index1] - in[index0]);
        }
    }

    // Rebase back into [0, numInput)-relative terms: only the fractional
    // remainder carries into the next call, since each call's inChannels is a
    // fresh, differently-sized array (see class header).
    readPos_ = posBefore + double(numOutput) * step - double(numInput);
}

int IdentityTimeStretcher::outputLatencyFrames() const
{
    return 0;
}

} // namespace djapp
