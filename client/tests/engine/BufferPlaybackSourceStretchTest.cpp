// Unit C / M10: BufferPlaybackSource's real-stretcher render pipeline.
//
// These tests are additive to BufferPlaybackSourceTest.cpp (which pins the
// no-arg, IdentityTimeStretcher-backed constructor and is not touched here)
// and exercise the new explicit BufferPlaybackSource(std::unique_ptr<TimeStretcher>)
// constructor with a real SignalsmithTimeStretcher, per the "Test impact"
// section of the Unit C spec: pitch/speed independence, the sample-rate
// mismatch regression, the small-seek-no-reset regression, large
// seek/load robustness, the end-of-track drain regression, the per-block
// rounding drift regression, and the position-latency domain-mixing
// regression (checked at both rate 1.0 and a tempo-changed rate).
//
// A real STFT-based stretcher is not an exact algorithm, so every numeric
// check below uses a generous, explicitly-reasoned tolerance: the goal is
// to catch the specific named regression, not to pin exact sample values.

#include "engine/BufferPlaybackSource.h"
#include "engine/SignalsmithTimeStretcher.h"
#include "engine/TimeStretcher.h"
#include "model/Types.h"
#include "repository/AudioRepository.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{

// A clean sine at a known frequency: lets pitch (dominant frequency) and
// speed (source-position consumption rate) be measured independently and
// cheaply, which a ramp signal cannot give us once real time-stretching is
// involved.
std::shared_ptr<const djapp::LoadedAudio> makeSineAudio(int numSamples, double sampleRate, double freqHz,
                                                          int numChannels = 1, float amplitude = 0.8f)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        const float value = amplitude * (float)std::sin(phaseIncrement * (double)i);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, value);
    }

    return std::make_shared<const djapp::LoadedAudio>(djapp::LoadedAudio{std::move(buffer), sampleRate});
}

juce::AudioBuffer<float> renderBlock(djapp::BufferPlaybackSource& source, int numChannels, int numSamples)
{
    juce::AudioBuffer<float> scratch(numChannels, numSamples);
    scratch.clear();
    juce::AudioSourceChannelInfo info(&scratch, 0, numSamples);
    source.getNextAudioBlock(info);
    return scratch;
}

float computeRms(const juce::AudioBuffer<float>& buffer, int channel, int numSamples)
{
    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double v = (double)buffer.getSample(channel, i);
        sumSquares += v * v;
    }
    return (float)std::sqrt(sumSquares / (double)numSamples);
}

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(buffer.getSample(ch, i)))
                return false;
    return true;
}

// Up-crossing zero-crossing frequency estimate over a contiguous run of
// samples (concatenated across block boundaries, which is safe because the
// audio itself is continuous even though it was rendered in separate
// getNextAudioBlock calls). Approximate, but adequate to distinguish "right
// pitch" from "off by ~1.5x" or "off by ~2x" or "off by ~8.8%", which is all
// these tests need.
double estimateFrequencyHz(const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i)
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
            ++crossings;

    const double durationSeconds = (double)samples.size() / sampleRate;
    return durationSeconds > 0.0 ? (double)crossings / durationSeconds : 0.0;
}

} // namespace

TEST_CASE("BufferPlaybackSource constructed with a real SignalsmithTimeStretcher plays finite audio",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int sourceLength = 44100; // 1s
    constexpr int blockSize = 512;

    djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeSineAudio(sourceLength, sampleRate, 440.0));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPitchOffsetSemitones(0.0f);
    source.setPlaying(true);

    for (int b = 0; b < 10; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        CHECK(allSamplesFinite(block, 2, blockSize));
    }
    CHECK(source.isPlaying());
}

TEST_CASE("BufferPlaybackSource with a real stretcher: playbackRate changes speed without changing pitch",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 5 * 44100; // 5s: generous margin over what priming + measurement can consume
    constexpr int blockSize = 512;
    // ~680ms of priming: generously above a typical STFT stretcher's startup
    // latency (on the order of a few thousand samples), so the measurement
    // window below reflects steady-state output, not the startup transient.
    constexpr int primeBlocks = 60;
    // ~93ms @44.1kHz -> ~41 cycles of a 440Hz tone: enough for a stable
    // zero-crossing frequency estimate.
    constexpr int measureBlocks = 8;

    auto measure = [&](float rate)
    {
        djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source.setGain(1.0f);
        source.setPlaybackRate(rate);
        source.setPitchOffsetSemitones(0.0f);
        source.setRepeat(true); // keep the source from self-stopping mid-measurement
        source.setPlaying(true);

        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(source, 2, blockSize);

        const double positionBefore = source.getCurrentPositionSeconds();

        std::vector<float> samples;
        samples.reserve(measureBlocks * blockSize);
        for (int b = 0; b < measureBlocks; ++b)
        {
            auto block = renderBlock(source, 2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                samples.push_back(block.getSample(0, i));
        }

        const double positionAfter = source.getCurrentPositionSeconds();
        return std::make_pair(estimateFrequencyHz(samples, sampleRate), positionAfter - positionBefore);
    };

    const auto [freqAtRate1, consumedAtRate1] = measure(1.0f);
    const auto [freqAtRate1_5, consumedAtRate1_5] = measure(1.5f);

    // Pitch-preserving: the dominant output frequency must stay close to the
    // source's own 440Hz regardless of tempo, at both rates.
    CHECK(freqAtRate1 == Catch::Approx(sourceFreqHz).epsilon(0.1));
    CHECK(freqAtRate1_5 == Catch::Approx(sourceFreqHz).epsilon(0.1));

    // Speed does change: at rate 1.5 the same fixed-duration output window
    // consumes ~1.5x as much source material. 25% tolerance is generous
    // because this is a ratio of two short, already-approximate position
    // deltas, but still cleanly distinguishes "no effect" (ratio ~1.0) from
    // the expected ~1.5x.
    const double consumedRatio = consumedAtRate1_5 / consumedAtRate1;
    CHECK(consumedRatio == Catch::Approx(1.5).epsilon(0.25));
}

TEST_CASE("BufferPlaybackSource with a real stretcher: pitchOffsetSemitones changes pitch without changing speed",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 5 * 44100;
    constexpr int blockSize = 512;
    constexpr int primeBlocks = 60;
    constexpr int measureBlocks = 8;

    auto measure = [&](float pitchOffsetSemitones)
    {
        djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source.setGain(1.0f);
        source.setPlaybackRate(1.0f);
        source.setPitchOffsetSemitones(pitchOffsetSemitones);
        source.setRepeat(true);
        source.setPlaying(true);

        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(source, 2, blockSize);

        const double positionBefore = source.getCurrentPositionSeconds();

        std::vector<float> samples;
        samples.reserve(measureBlocks * blockSize);
        for (int b = 0; b < measureBlocks; ++b)
        {
            auto block = renderBlock(source, 2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                samples.push_back(block.getSample(0, i));
        }

        const double positionAfter = source.getCurrentPositionSeconds();
        return std::make_pair(estimateFrequencyHz(samples, sampleRate), positionAfter - positionBefore);
    };

    const auto [freqAtPitch0, consumedAtPitch0] = measure(0.0f);
    const auto [freqAtPitch12, consumedAtPitch12] = measure(12.0f);

    // +12 semitones = one octave up = frequency x2. 15% tolerance: pitch
    // shifting via a phase vocoder is not perfectly exact even for a clean
    // sine, but a correct +1 octave shift should land well inside this band,
    // while "pitch offset had no effect" (~1x) would clearly miss it.
    CHECK(freqAtPitch12 == Catch::Approx(2.0 * freqAtPitch0).epsilon(0.15));

    // Speed (source consumption rate) must stay the same: the pitch shift
    // alone must not change how fast the source is consumed.
    const double consumedRatio = consumedAtPitch12 / consumedAtPitch0;
    CHECK(consumedRatio == Catch::Approx(1.0).epsilon(0.15));
}

TEST_CASE("BufferPlaybackSource with a real stretcher: a source/device sample-rate mismatch still plays at the "
          "correct pitch, not detuned by the device/source ratio",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    // Regression for draft 1's bug: folding the sample-rate ratio into the
    // stretcher's tempo ratio instead of doing real resampling would treat
    // raw, un-resampled source-rate samples as if they were already at the
    // device rate, detuning playback by exactly deviceSampleRate/sourceSampleRate.
    constexpr double sourceSampleRate = 44100.0;
    constexpr double deviceSampleRate = 48000.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = (int)(5 * sourceSampleRate); // 5s @44.1kHz
    constexpr int blockSize = 512;                            // device-rate output block
    constexpr int primeBlocks = 70;
    constexpr int measureBlocks = 8;

    djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
    source.prepareToPlay(blockSize, deviceSampleRate);
    source.load(makeSineAudio(sourceLength, sourceSampleRate, sourceFreqHz));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setPitchOffsetSemitones(0.0f);
    source.setRepeat(true);
    source.setPlaying(true);

    for (int b = 0; b < primeBlocks; ++b)
        renderBlock(source, 2, blockSize);

    std::vector<float> samples;
    samples.reserve(measureBlocks * blockSize);
    for (int b = 0; b < measureBlocks; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        for (int i = 0; i < blockSize; ++i)
            samples.push_back(block.getSample(0, i));
    }

    // Output samples are at the device rate, so the frequency estimate must
    // use deviceSampleRate here, not sourceSampleRate.
    const double measuredFreqHz = estimateFrequencyHz(samples, deviceSampleRate);

    // The specific wrong frequency a "folded SR-into-tempo-ratio"
    // implementation would visibly produce.
    const double wrongFoldedFreqHz = sourceFreqHz * (deviceSampleRate / sourceSampleRate); // ~478.9 Hz

    // Correct implementation: genuinely resampled to device rate before
    // stretching at tempo ratio 1.0, so pitch matches the source's true
    // 440Hz. 5% tolerance is comfortably tighter than the ~8.8% detune the
    // regression would produce (48000/44100 ~= 1.088), while still being
    // generous for a real STFT-based pitch estimate off a short window.
    CHECK(measuredFreqHz == Catch::Approx(sourceFreqHz).epsilon(0.05));
    CHECK(measuredFreqHz != Catch::Approx(wrongFoldedFreqHz).epsilon(0.03));
}

TEST_CASE("BufferPlaybackSource with a real stretcher: a small requestSeek well under the 200ms reset threshold "
          "does not cause an audible dropout",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 5 * 44100;
    constexpr int blockSize = 512;
    constexpr int primeBlocks = 40;
    constexpr int checkBlocks = 8;
    // 10ms: well under the spec's 200ms threshold for "a real discontinuity",
    // modeling a drift-correction-sized nudge (e.g. PositionClock's 5s
    // resync write) that must NOT reset the stretcher.
    constexpr double smallSeekDeltaSeconds = 0.01;

    auto makeAndPrime = [&]()
    {
        auto source = std::make_unique<djapp::BufferPlaybackSource>(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source->prepareToPlay(blockSize, sampleRate);
        source->load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source->setGain(1.0f);
        source->setPlaybackRate(1.0f);
        source->setRepeat(true);
        source->setPlaying(true);
        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(*source, 2, blockSize);
        return source;
    };

    auto unseeked = makeAndPrime();
    auto seeked = makeAndPrime();

    const double currentPos = seeked->getCurrentPositionSeconds();
    seeked->requestSeek(currentPos + smallSeekDeltaSeconds);

    for (int b = 0; b < checkBlocks; ++b)
    {
        auto blockUnseeked = renderBlock(*unseeked, 2, blockSize);
        auto blockSeeked = renderBlock(*seeked, 2, blockSize);

        const float rmsUnseeked = computeRms(blockUnseeked, 0, blockSize);
        const float rmsSeeked = computeRms(blockSeeked, 0, blockSize);

        // A dropout would show up as the seeked instance's block energy
        // collapsing toward silence (a stretcher-reset startup transient)
        // relative to the unseeked continuation's steady output. 40% is a
        // generous floor - a small seek does shift phase/position slightly,
        // which can legitimately perturb per-block energy somewhat for a
        // windowed sine, but must not come close to silence.
        CHECK(rmsSeeked > rmsUnseeked * 0.4f);
        // Absolute floor: well below a full-amplitude sine's own ~0.57 RMS,
        // but well above true silence (kSilenceMargin-scale values).
        CHECK(rmsSeeked > 0.1f);
    }
}

TEST_CASE("BufferPlaybackSource with a real stretcher: a large requestSeek or a load() mid-playback stays finite "
          "and does not crash",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 5 * 44100;
    constexpr int blockSize = 512;
    constexpr int primeBlocks = 20;
    constexpr int checkBlocks = 10;

    SECTION("large requestSeek")
    {
        djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source.setGain(1.0f);
        source.setPlaybackRate(1.0f);
        source.setRepeat(true);
        source.setPlaying(true);

        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(source, 2, blockSize);

        // 2.5s jump: far beyond the 200ms discontinuity threshold, forces a
        // real stretcher reset mid-playback.
        source.requestSeek(2.5);

        for (int b = 0; b < checkBlocks; ++b)
        {
            auto block = renderBlock(source, 2, blockSize);
            CHECK(allSamplesFinite(block, 2, blockSize));
        }
    }

    SECTION("load() mid-playback")
    {
        djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source.setGain(1.0f);
        source.setPlaybackRate(1.0f);
        source.setRepeat(true);
        source.setPlaying(true);

        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(source, 2, blockSize);

        // A brand new track always resets the stretcher unconditionally per
        // the spec, regardless of numeric distance from the current position.
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz * 2.0));

        for (int b = 0; b < checkBlocks; ++b)
        {
            auto block = renderBlock(source, 2, blockSize);
            CHECK(allSamplesFinite(block, 2, blockSize));
        }
    }
}

TEST_CASE("BufferPlaybackSource with a real stretcher: end-of-track with repeat off eventually stops, draining "
          "audible tail content instead of cutting off instantly",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    // Regression for a bug where the old design cut off the last fraction of
    // a second of every track: the drain must let the stretcher emit its own
    // buffered tail before playing_ goes false, not stop the instant the raw
    // pull runs dry.
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 20000; // ~453ms
    constexpr int blockSize = 512;

    djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
    source.setGain(1.0f);
    source.setPlaybackRate(1.0f);
    source.setRepeat(false);
    source.setPlaying(true);

    // At rate 1.0 with matching source/device rates, the pull stage consumes
    // roughly one source sample per output sample, so the raw source content
    // is exhausted after roughly sourceLength / blockSize blocks. This is
    // only an estimate (the exact stop sample depends on interpolation edge
    // handling), used purely to bound how many blocks to render and to pick
    // a window to look for tail content in - not an exact prediction.
    constexpr int approxDryBlock = sourceLength / blockSize; // ~39
    // Generous bound: a well-behaved drain must be finite and short relative
    // to letting the render loop run far longer than any plausible latency.
    constexpr int maxBlocks = approxDryBlock + 200;

    std::vector<float> rmsPerBlock;
    int stopBlock = -1;
    for (int b = 0; b < maxBlocks; ++b)
    {
        auto block = renderBlock(source, 2, blockSize);
        rmsPerBlock.push_back(computeRms(block, 0, blockSize));
        if (!source.isPlaying())
        {
            stopBlock = b;
            break;
        }
    }

    // Must actually stop - bounded, not a hang or an indefinite drain.
    REQUIRE(stopBlock >= 0);

    // Not immediate: the source itself takes ~approxDryBlock blocks to
    // exhaust, so the stop should land at or after that point.
    CHECK(stopBlock >= approxDryBlock - 2);
    // Bounded: the drain (outputLatencyFrames_ + 1 output frames) must not
    // run for anywhere near this generously long a margin.
    CHECK(stopBlock <= approxDryBlock + 60);

    // Regression check: at least one block at or after the estimated dry
    // point still carries audible content, rather than the output going
    // silent the instant the raw pull ran dry.
    bool sawAudibleTailPastDryPoint = false;
    for (int b = std::max(0, approxDryBlock - 3); b < (int)rmsPerBlock.size(); ++b)
    {
        // Well above the ~1e-6 silence margin used elsewhere in this suite,
        // comfortably below a full-amplitude sine's own RMS (~0.57).
        if (rmsPerBlock[(size_t)b] > 0.05f)
            sawAudibleTailPastDryPoint = true;
    }
    CHECK(sawAudibleTailPastDryPoint);
}

TEST_CASE("BufferPlaybackSource with a real stretcher: a non-1.0 rate holds the correct long-run duration without "
          "cumulative per-block rounding drift",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    // Regression for draft 2's bug: rounding numOutput*rate to the nearest
    // integer every block (instead of carrying the fractional remainder
    // forward) discards a remainder that does not average to zero for a
    // constant rate, integrating into real tempo drift.
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 400000; // ~9.07s: comfortably more than the ~6.4s this test consumes at rate 1.2
    // 512 mod 5 == 2 -> numOutput*1.2 has a .4 fractional part on every
    // block (since 1.2 = 6/5), close to the worst case for a naive round()'s
    // systematic bias.
    constexpr int blockSize = 512;
    constexpr float rate = 1.2f;
    // Past a typical STFT stretcher's startup latency, so both the "before"
    // and "after" position readings share the same (constant, saturated)
    // latency term and it cancels out of the delta below - isolating this
    // test from the separate latency-compensation concern the next test
    // covers.
    constexpr int primeBlocks = 60;
    constexpr int measureBlocks = 400; // "at least a few hundred blocks" per spec

    djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
    source.prepareToPlay(blockSize, sampleRate);
    source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
    source.setGain(1.0f);
    source.setPlaybackRate(rate);
    source.setRepeat(true); // avoid a self-stop/wrap confounding the long-run position measurement
    source.setPlaying(true);

    for (int b = 0; b < primeBlocks; ++b)
        renderBlock(source, 2, blockSize);

    const double positionBefore = source.getCurrentPositionSeconds();

    for (int b = 0; b < measureBlocks; ++b)
        renderBlock(source, 2, blockSize);

    const double positionAfter = source.getCurrentPositionSeconds();
    const double observedConsumedSeconds = positionAfter - positionBefore;
    const double outputSecondsRendered = (double)(measureBlocks * blockSize) / sampleRate;
    const double expectedConsumedSeconds = outputSecondsRendered * (double)rate;

    // A per-block round() without a fractional accumulator carries a
    // systematic (non-cancelling) bias of exactly 0.4 samples/block at this
    // (blockSize, rate) pairing (512*1.2 = 614.4, round() always discards
    // the same 0.4), integrating to exactly 160 samples (~3.63ms) of drift
    // over 400 blocks. A 1ms margin is generous relative to the correct
    // implementation (an exact, deterministic accumulator - no reason to be
    // off by more than floating-point noise) while sitting comfortably below
    // the ~3.63ms the bug would produce.
    CHECK(observedConsumedSeconds == Catch::Approx(expectedConsumedSeconds).margin(0.001));
}

TEST_CASE("BufferPlaybackSource with a real stretcher: getCurrentPositionSeconds tracks real elapsed time after "
          "priming past the stretcher's output latency, at both rate 1.0 and a tempo-changed rate",
          "[engine][BufferPlaybackSource][TimeStretcher]")
{
    // Regression for draft 2's bug: mixing outputLatencyFrames_ (an
    // output-frame count) directly into a source-domain position without
    // converting through `rate` produces a position-latency formula that is
    // only correct at rate == 1.0 - invisible to every test that only checks
    // the default rate. This test checks the same relationship at rate 1.2,
    // where the spec says the bug was not invisible.
    constexpr double sampleRate = 44100.0;
    constexpr double sourceFreqHz = 440.0;
    constexpr int sourceLength = 200000; // ~4.5s: comfortably more than what priming at rate 1.2 consumes (~1.1s)
    constexpr int blockSize = 512;
    // Generously above a typical STFT stretcher's output latency (a few
    // thousand samples at most).
    constexpr int primeBlocks = 80;

    auto measureGapSeconds = [&](float rate)
    {
        djapp::BufferPlaybackSource source(std::make_unique<djapp::SignalsmithTimeStretcher>());
        source.prepareToPlay(blockSize, sampleRate);
        source.load(makeSineAudio(sourceLength, sampleRate, sourceFreqHz));
        source.setGain(1.0f);
        source.setPlaybackRate(rate);
        source.setRepeat(true);
        source.setPlaying(true);

        for (int b = 0; b < primeBlocks; ++b)
            renderBlock(source, 2, blockSize);

        // "Ideal" position if the stretcher had zero output latency: with
        // matching source/device sample rates and the accumulator-based pull
        // (pinned by the drift test above), the raw pull head advances by
        // exactly `rate` seconds of source per second of output rendered.
        const double idealPositionSeconds = ((double)primeBlocks * blockSize / sampleRate) * (double)rate;
        const double actualPositionSeconds = source.getCurrentPositionSeconds();

        // The reported position lags the raw pull head by the stretcher's
        // still-unpaid startup latency - this is the gap this test measures.
        return idealPositionSeconds - actualPositionSeconds;
    };

    const double gapAtRate1 = measureGapSeconds(1.0f);
    const double gapAtRate1_2 = measureGapSeconds(1.2f);

    // The reported position must genuinely lag (not lead) the ideal
    // zero-latency position at both rates.
    CHECK(gapAtRate1 > 0.0);
    CHECK(gapAtRate1_2 > 0.0);

    // outputLatencyFrames_ is refreshed once per reset and is not itself a
    // function of the per-call tempo ratio (prepare()/reset() take no rate
    // argument), so it should be the same fixed output-frame count in both
    // measurements above. Converting that fixed output-frame lag into the
    // position domain must multiply through `rate`; a formula that skips
    // that conversion produces a gap of the same fixed size regardless of
    // rate (gapRatio ~= 1.0, exactly the shape the spec says was invisible
    // at rate 1.0). The corrected formula's gap should instead scale up by
    // ~rate (gapRatio ~= 1.2), since the same output-frame lag maps to more
    // source-domain seconds at a higher rate.
    const double gapRatio = gapAtRate1_2 / gapAtRate1;
    CHECK(gapRatio == Catch::Approx(1.2).epsilon(0.15));
    CHECK(std::abs(gapRatio - 1.0) > 0.05);
}
