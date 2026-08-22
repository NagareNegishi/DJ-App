#include "engine/IdentityTimeStretcher.h"
#include "engine/SignalsmithTimeStretcher.h"
#include "engine/TimeStretcher.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{

// Independently mirrors IdentityTimeStretcher's documented per-sample formula
// (spec: "IdentityTimeStretcher" section) so tests can predict exact expected
// output for an arbitrary sequence of process() calls without depending on
// the implementation under test for anything but comparison. Carries its own
// readPos across calls exactly as the spec's rebase rule describes, so a
// sequence of oracle.process() calls models the whole-sequence phase
// continuity contract, including the numInput==0/numOutput==0 no-op (handled
// by the caller skipping the oracle call entirely, matching "leave readPos_
// unchanged").
struct IdentityOracle
{
    double readPos = 0.0;

    std::vector<float> process(const std::vector<float>& in, int numOutput)
    {
        const int numInput = (int)in.size();
        std::vector<float> out((size_t)numOutput, 0.0f);
        if (numInput == 0 || numOutput == 0)
            return out;

        const double step = double(numInput) / double(numOutput);
        for (int o = 0; o < numOutput; ++o)
        {
            const double pos = readPos + (double)o * step;
            int index0 = (int)std::floor(pos);
            index0 = std::clamp(index0, 0, numInput - 1);
            const int index1 = std::min(index0 + 1, numInput - 1);
            const double frac = pos - std::floor(pos);
            out[(size_t)o] = in[(size_t)index0] + (float)frac * (in[(size_t)index1] - in[(size_t)index0]);
        }
        readPos = readPos + (double)numOutput * step - (double)numInput;
        return out;
    }
};

constexpr float kTightMargin = 1.0e-4f;
constexpr float kPi = 3.14159265358979323846f;

} // namespace

// --- IdentityTimeStretcher ---

TEST_CASE("IdentityTimeStretcher numInput == numOutput reproduces the input exactly with zero drift "
          "across many consecutive calls",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);

    constexpr int blockSize = 7;
    for (int call = 0; call < 20; ++call)
    {
        float in[blockSize];
        for (int i = 0; i < blockSize; ++i)
            in[i] = (float)(call * 100 + i); // distinct content each call, unrelated to any previous call

        float out[blockSize];
        const float* inCh[1] = { in };
        float* outCh[1] = { out };
        stretcher.process(inCh, blockSize, outCh, blockSize);

        // step == 1.0 exactly (7/7), so readPos's fractional part stays
        // exactly 0 on every call: this must be exact reproduction, not an
        // approximation that could quietly drift over many calls.
        for (int i = 0; i < blockSize; ++i)
            CHECK(out[i] == Catch::Approx(in[i]).margin(1.0e-6f));
    }
}

TEST_CASE("IdentityTimeStretcher matches the documented per-sample interpolation formula across a dozen "
          "consecutive calls with varying numInput/numOutput ratios, without accumulating drift",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);
    stretcher.reset();

    IdentityOracle oracle;

    // At least a dozen calls, mixing upsampling, downsampling, and 1:1
    // ratios, so a wrong (e.g. block-local) implementation would show
    // growing disagreement with the oracle rather than passing by accident.
    const std::vector<std::pair<int, int>> calls = { { 4, 4 }, { 6, 3 }, { 3, 6 }, { 5, 4 }, { 4, 5 }, { 7, 2 },
                                                       { 2, 7 }, { 5, 5 }, { 9, 4 }, { 4, 9 }, { 6, 6 }, { 3, 4 } };

    int totalInput = 0;
    for (const auto& [numInput, numOutput] : calls)
        totalInput += numInput;

    std::vector<float> ramp((size_t)totalInput);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float)i;

    int offset = 0;
    for (const auto& [numInput, numOutput] : calls)
    {
        std::vector<float> inSlice(ramp.begin() + offset, ramp.begin() + offset + numInput);
        const std::vector<float> expected = oracle.process(inSlice, numOutput);

        std::vector<float> actual((size_t)numOutput, -999.0f);
        const float* inCh[1] = { inSlice.data() };
        float* outCh[1] = { actual.data() };
        stretcher.process(inCh, numInput, outCh, numOutput);

        for (int o = 0; o < numOutput; ++o)
            CHECK(actual[(size_t)o] == Catch::Approx(expected[(size_t)o]).margin(kTightMargin));

        offset += numInput;
    }
}

TEST_CASE("IdentityTimeStretcher numInput != numOutput produces the documented linear stretch, "
          "including the end-of-block clamp",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);

    float in[2] = { 10.0f, 20.0f };
    float out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float* inCh[1] = { in };
    float* outCh[1] = { out };
    stretcher.process(inCh, 2, outCh, 4);

    // step = numInput / numOutput = 0.5, readPos sequence 0, 0.5, 1.0, 1.5.
    // o=0: index0=0, frac=0            -> 10
    // o=1: index0=0, index1=1, frac=.5 -> 15
    // o=2: index0=1, frac=0            -> 20
    // o=3: floor(1.5)=1, clamped to numInput-1=1; index1=min(2,1)=1, frac=.5
    //      -> 20 + .5*(20-20) = 20 (held at the last input sample per the
    //      documented clamp, not extrapolated past the block)
    CHECK(out[0] == Catch::Approx(10.0f).margin(kTightMargin));
    CHECK(out[1] == Catch::Approx(15.0f).margin(kTightMargin));
    CHECK(out[2] == Catch::Approx(20.0f).margin(kTightMargin));
    CHECK(out[3] == Catch::Approx(20.0f).margin(kTightMargin));
}

TEST_CASE("IdentityTimeStretcher setPitchSemitones is a no-op regardless of value",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    constexpr int numInput = 6;
    constexpr int numOutput = 4;
    float in[numInput] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };

    auto runWithPitch = [&](float semitones)
    {
        djapp::IdentityTimeStretcher stretcher;
        stretcher.prepare(1, 44100.0);
        stretcher.setPitchSemitones(semitones);

        std::vector<float> out((size_t)numOutput, 0.0f);
        const float* inCh[1] = { in };
        float* outCh[1] = { out.data() };
        stretcher.process(inCh, numInput, outCh, numOutput);
        return out;
    };

    const std::vector<float> baseline = runWithPitch(0.0f);
    for (float semis : { -12.0f, -5.5f, 3.3f, 12.0f })
    {
        const std::vector<float> out = runWithPitch(semis);
        for (int o = 0; o < numOutput; ++o)
            CHECK(out[(size_t)o] == Catch::Approx(baseline[(size_t)o]).margin(1.0e-6f));
    }
}

TEST_CASE("IdentityTimeStretcher process() with numInput == 0 or numOutput == 0 is a defined no-op "
          "that does not write output and does not crash",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);

    SECTION("numInput == 0 leaves the output buffer untouched")
    {
        const float dummyIn[1] = { 0.0f };
        float sentinel[3] = { 111.0f, 111.0f, 111.0f };
        const float* inCh[1] = { dummyIn };
        float* outCh[1] = { sentinel };

        stretcher.process(inCh, 0, outCh, 3);

        CHECK(sentinel[0] == 111.0f);
        CHECK(sentinel[1] == 111.0f);
        CHECK(sentinel[2] == 111.0f);
    }

    SECTION("numOutput == 0 leaves the output buffer untouched and does not crash")
    {
        const float dummyIn[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        float sentinel[1] = { 222.0f };
        const float* inCh[1] = { dummyIn };
        float* outCh[1] = { sentinel };

        stretcher.process(inCh, 4, outCh, 0);

        CHECK(sentinel[0] == 222.0f);
    }
}

TEST_CASE("IdentityTimeStretcher no-op calls (numInput == 0 or numOutput == 0) do not disturb phase "
          "continuity for the surrounding calls",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);
    stretcher.reset();
    IdentityOracle oracle;

    std::vector<float> ramp(50);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float)i;
    int offset = 0;

    auto runNormalCall = [&](int numInput, int numOutput)
    {
        std::vector<float> inSlice(ramp.begin() + offset, ramp.begin() + offset + numInput);
        const std::vector<float> expected = oracle.process(inSlice, numOutput);

        std::vector<float> actual((size_t)numOutput, -999.0f);
        const float* inCh[1] = { inSlice.data() };
        float* outCh[1] = { actual.data() };
        stretcher.process(inCh, numInput, outCh, numOutput);

        for (int o = 0; o < numOutput; ++o)
            CHECK(actual[(size_t)o] == Catch::Approx(expected[(size_t)o]).margin(kTightMargin));

        offset += numInput;
    };

    runNormalCall(4, 4);

    // numInput == 0: defined no-op, must not perturb continuity for the calls around it.
    {
        const float dummyIn[1] = { 0.0f };
        float sentinel[3] = { 111.0f, 111.0f, 111.0f };
        const float* inCh[1] = { dummyIn };
        float* outCh[1] = { sentinel };
        stretcher.process(inCh, 0, outCh, 3);
        CHECK(sentinel[0] == 111.0f);
        CHECK(sentinel[1] == 111.0f);
        CHECK(sentinel[2] == 111.0f);
    }

    runNormalCall(3, 5);

    // numOutput == 0: defined no-op, must not crash and must not perturb continuity.
    {
        const float dummyIn[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float sentinel[1] = { 222.0f };
        const float* inCh[1] = { dummyIn };
        float* outCh[1] = { sentinel };
        stretcher.process(inCh, 4, outCh, 0);
        CHECK(sentinel[0] == 222.0f);
    }

    runNormalCall(4, 3);
}

TEST_CASE("IdentityTimeStretcher outputLatencyFrames returns 0", "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(2, 44100.0);
    CHECK(stretcher.outputLatencyFrames() == 0);
}

TEST_CASE("IdentityTimeStretcher processes each channel independently for 1 and 2 channel configurations",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    SECTION("1 channel")
    {
        djapp::IdentityTimeStretcher stretcher;
        stretcher.prepare(1, 44100.0);

        float in[3] = { 1.0f, 2.0f, 3.0f };
        float out[3];
        const float* inCh[1] = { in };
        float* outCh[1] = { out };
        stretcher.process(inCh, 3, outCh, 3);

        for (int i = 0; i < 3; ++i)
            CHECK(out[i] == Catch::Approx(in[i]).margin(1.0e-6f));
    }

    SECTION("2 channels")
    {
        djapp::IdentityTimeStretcher stretcher;
        stretcher.prepare(2, 44100.0);

        float inL[3] = { 1.0f, 2.0f, 3.0f };
        float inR[3] = { 10.0f, 20.0f, 30.0f };
        float outL[3];
        float outR[3];
        const float* inCh[2] = { inL, inR };
        float* outCh[2] = { outL, outR };
        stretcher.process(inCh, 3, outCh, 3);

        for (int i = 0; i < 3; ++i)
        {
            CHECK(outL[i] == Catch::Approx(inL[i]).margin(1.0e-6f));
            CHECK(outR[i] == Catch::Approx(inR[i]).margin(1.0e-6f));
        }
    }
}

TEST_CASE("IdentityTimeStretcher re-preparing with a different channel count and sample rate does not "
          "crash on the next process() call",
          "[engine][TimeStretcher][IdentityTimeStretcher]")
{
    djapp::IdentityTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);

    float in1[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float out1[4];
    const float* inCh1[1] = { in1 };
    float* outCh1[1] = { out1 };
    stretcher.process(inCh1, 4, outCh1, 4);

    stretcher.prepare(2, 48000.0); // simulated track change: different channel count and sample rate

    float inL[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    float inR[5] = { 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };
    float outL[5];
    float outR[5];
    const float* inCh2[2] = { inL, inR };
    float* outCh2[2] = { outL, outR };
    stretcher.process(inCh2, 5, outCh2, 5);

    for (int i = 0; i < 5; ++i)
    {
        CHECK(outL[i] == Catch::Approx(inL[i]).margin(1.0e-6f));
        CHECK(outR[i] == Catch::Approx(inR[i]).margin(1.0e-6f));
    }
}

// --- SignalsmithTimeStretcher ---

TEST_CASE("SignalsmithTimeStretcher at rate 1.0 and pitch 0 approximately reproduces a sustained input signal",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 20;
    constexpr float frequency = 440.0f;

    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(1, sampleRate);
    stretcher.setPitchSemitones(0.0f);
    stretcher.reset();

    const int latency = stretcher.outputLatencyFrames();
    REQUIRE(latency >= 0);

    std::vector<float> inputAll((size_t)(blockSize * numBlocks));
    std::vector<float> outputAll((size_t)(blockSize * numBlocks), 0.0f);

    for (int b = 0; b < numBlocks; ++b)
    {
        std::vector<float> in((size_t)blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const int globalIndex = b * blockSize + i;
            in[(size_t)i] = std::sin(2.0f * kPi * frequency * (float)globalIndex / (float)sampleRate);
            inputAll[(size_t)globalIndex] = in[(size_t)i];
        }

        std::vector<float> out((size_t)blockSize, 0.0f);
        const float* inCh[1] = { in.data() };
        float* outCh[1] = { out.data() };
        stretcher.process(inCh, blockSize, outCh, blockSize);

        for (int i = 0; i < blockSize; ++i)
            outputAll[(size_t)(b * blockSize + i)] = out[(size_t)i];
    }

    // Tolerance reasoning: at rate 1.0 / pitch 0, a phase-vocoder is not
    // required to be a bit-exact passthrough -- STFT windowing/overlap-add
    // introduces some smoothing even at "unity" settings, and the exact
    // alignment of outputLatencyFrames() against the analysis window's
    // center is an implementation detail this spec doesn't pin precisely.
    // We skip the first two blocks entirely (letting internal buffering
    // fully warm up past outputLatencyFrames()) and, over the remaining
    // latency-aligned samples, require the mean absolute error against the
    // shifted input to be a small fraction of the signal's peak amplitude
    // (1.0) -- tight enough to catch a wiring bug (silence, noise, wrong
    // channel, ignored latency) but loose enough to tolerate legitimate
    // windowing smoothing.
    const int compareStart = 2 * blockSize;
    const int compareEnd = blockSize * numBlocks;
    double sumAbsError = 0.0;
    int comparedSamples = 0;
    for (int i = compareStart; i < compareEnd; ++i)
    {
        const int sourceIndex = i - latency;
        if (sourceIndex < 0 || sourceIndex >= (int)inputAll.size())
            continue;
        sumAbsError += std::abs((double)outputAll[(size_t)i] - (double)inputAll[(size_t)sourceIndex]);
        ++comparedSamples;
    }

    REQUIRE(comparedSamples > 0);
    const double meanAbsError = sumAbsError / comparedSamples;
    CHECK(meanAbsError < 0.3);
}

TEST_CASE("SignalsmithTimeStretcher a nonzero pitch shift makes the output diverge from a plain "
          "latency-aligned copy of the input",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    constexpr int numBlocks = 20;
    constexpr float frequency = 440.0f;

    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(1, sampleRate);
    stretcher.setPitchSemitones(12.0f); // one octave up: unambiguous, large spectral change
    stretcher.reset();

    const int latency = stretcher.outputLatencyFrames();

    std::vector<float> inputAll((size_t)(blockSize * numBlocks));
    std::vector<float> outputAll((size_t)(blockSize * numBlocks), 0.0f);

    for (int b = 0; b < numBlocks; ++b)
    {
        std::vector<float> in((size_t)blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const int globalIndex = b * blockSize + i;
            in[(size_t)i] = std::sin(2.0f * kPi * frequency * (float)globalIndex / (float)sampleRate);
            inputAll[(size_t)globalIndex] = in[(size_t)i];
        }

        std::vector<float> out((size_t)blockSize, 0.0f);
        const float* inCh[1] = { in.data() };
        float* outCh[1] = { out.data() };
        stretcher.process(inCh, blockSize, outCh, blockSize);

        for (int i = 0; i < blockSize; ++i)
            outputAll[(size_t)(b * blockSize + i)] = out[(size_t)i];
    }

    // Same methodology and warm-up allowance as the rate-1.0/pitch-0 test
    // above, but here we expect the opposite outcome: a full-octave pitch
    // shift on a pure 440 Hz tone must NOT look like a latency-delayed copy
    // of the input -- if it did, setPitchSemitones would not be doing
    // anything. We only assert the mean absolute error clearly exceeds the
    // "approximately equal" threshold used for the unshifted case, not any
    // particular target frequency, keeping this cheap and robust to the
    // library's specific windowing/latency behavior.
    const int compareStart = 2 * blockSize;
    const int compareEnd = blockSize * numBlocks;
    double sumAbsError = 0.0;
    int comparedSamples = 0;
    for (int i = compareStart; i < compareEnd; ++i)
    {
        const int sourceIndex = i - latency;
        if (sourceIndex < 0 || sourceIndex >= (int)inputAll.size())
            continue;
        sumAbsError += std::abs((double)outputAll[(size_t)i] - (double)inputAll[(size_t)sourceIndex]);
        ++comparedSamples;
    }

    REQUIRE(comparedSamples > 0);
    const double meanAbsError = sumAbsError / comparedSamples;
    CHECK(meanAbsError > 0.3);
}

TEST_CASE("SignalsmithTimeStretcher reset() followed by fresh near-silent input does not crash and "
          "produces finite output",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr float frequency = 300.0f;

    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(1, sampleRate);
    stretcher.setPitchSemitones(3.0f);

    for (int b = 0; b < 5; ++b)
    {
        std::vector<float> in((size_t)blockSize);
        for (int i = 0; i < blockSize; ++i)
            in[(size_t)i] = std::sin(2.0f * kPi * frequency * (float)(b * blockSize + i) / (float)sampleRate);

        std::vector<float> out((size_t)blockSize, 0.0f);
        const float* inCh[1] = { in.data() };
        float* outCh[1] = { out.data() };
        stretcher.process(inCh, blockSize, outCh, blockSize);
    }

    stretcher.reset();
    stretcher.setPitchSemitones(0.0f);

    // "Silence-adjacent" input: near-zero but not exactly all-zero, since a
    // pathologically all-zero input is a degenerate case some spectral
    // algorithms special-case; a tiny-amplitude signal exercises the normal
    // code path immediately after reset() without assuming any particular
    // reconstructed value.
    std::vector<float> quietIn((size_t)blockSize);
    for (int i = 0; i < blockSize; ++i)
        quietIn[(size_t)i] = 1.0e-4f * std::sin(2.0f * kPi * frequency * (float)i / (float)sampleRate);

    std::vector<float> quietOut((size_t)blockSize, 0.0f);
    const float* inCh2[1] = { quietIn.data() };
    float* outCh2[1] = { quietOut.data() };
    stretcher.process(inCh2, blockSize, outCh2, blockSize);

    for (float v : quietOut)
        CHECK(std::isfinite(v));
}

TEST_CASE("SignalsmithTimeStretcher outputLatencyFrames returns a non-negative value after prepare()",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(2, 44100.0);
    CHECK(stretcher.outputLatencyFrames() >= 0);
}

TEST_CASE("SignalsmithTimeStretcher processes without crashing and produces finite output for 1 and 2 "
          "channel configurations",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;

    SECTION("1 channel")
    {
        djapp::SignalsmithTimeStretcher stretcher;
        stretcher.prepare(1, sampleRate);

        std::vector<float> in((size_t)blockSize, 0.1f);
        std::vector<float> out((size_t)blockSize, 0.0f);
        const float* inCh[1] = { in.data() };
        float* outCh[1] = { out.data() };
        stretcher.process(inCh, blockSize, outCh, blockSize);

        for (float v : out)
            CHECK(std::isfinite(v));
    }

    SECTION("2 channels")
    {
        djapp::SignalsmithTimeStretcher stretcher;
        stretcher.prepare(2, sampleRate);

        std::vector<float> inL((size_t)blockSize, 0.1f);
        std::vector<float> inR((size_t)blockSize, -0.1f);
        std::vector<float> outL((size_t)blockSize, 0.0f);
        std::vector<float> outR((size_t)blockSize, 0.0f);
        const float* inCh[2] = { inL.data(), inR.data() };
        float* outCh[2] = { outL.data(), outR.data() };
        stretcher.process(inCh, blockSize, outCh, blockSize);

        for (float v : outL)
            CHECK(std::isfinite(v));
        for (float v : outR)
            CHECK(std::isfinite(v));
    }
}

TEST_CASE("SignalsmithTimeStretcher re-preparing with a different channel count and sample rate does "
          "not crash on the next process() call",
          "[engine][TimeStretcher][SignalsmithTimeStretcher]")
{
    constexpr int blockSize = 256;

    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(1, 44100.0);

    std::vector<float> in1((size_t)blockSize, 0.2f);
    std::vector<float> out1((size_t)blockSize, 0.0f);
    const float* inCh1[1] = { in1.data() };
    float* outCh1[1] = { out1.data() };
    stretcher.process(inCh1, blockSize, outCh1, blockSize);

    stretcher.prepare(2, 48000.0); // simulated track change: different channel count and sample rate

    std::vector<float> inL((size_t)blockSize, 0.3f);
    std::vector<float> inR((size_t)blockSize, -0.3f);
    std::vector<float> outL((size_t)blockSize, 0.0f);
    std::vector<float> outR((size_t)blockSize, 0.0f);
    const float* inCh2[2] = { inL.data(), inR.data() };
    float* outCh2[2] = { outL.data(), outR.data() };
    stretcher.process(inCh2, blockSize, outCh2, blockSize);

    for (float v : outL)
        CHECK(std::isfinite(v));
    for (float v : outR)
        CHECK(std::isfinite(v));
}
