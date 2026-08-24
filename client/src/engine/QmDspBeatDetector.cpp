// engine/ — QmDspBeatDetector implementation. The only translation unit
// (besides QmDspBeatDetector.h, which stays qm-dsp-free) allowed to include
// qm-dsp headers directly.

#include "QmDspBeatDetector.h"

#include "dsp/onsets/DetectionFunction.h"
#include "dsp/tempotracking/TempoTrackV2.h"

#include <algorithm>
#include <vector>

namespace djapp
{

namespace
{

// DF analysis window: frameLength is 2x stepSize, the relationship
// DFConfig::frameLength's own comment documents ("usually 2*step"). Matches
// the parameters qm-vamp-plugins' BeatTrack.cpp (the real, reference
// consumer of this exact DetectionFunction/TempoTrackV2 pair, verified by
// reading its source) uses for its default beat tracker.
constexpr int frameLength = 1024;
constexpr int stepSize = 512;

// qm-dsp's DetectionFunction only accepts a single channel; average down.
std::vector<double> mixToMonoDouble(const juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    std::vector<double> mono(static_cast<size_t>(numSamples), 0.0);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const float* channelData = buffer.getReadPointer(channel);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] += channelData[i];
    }
    if (numChannels > 0)
    {
        const double scale = 1.0 / static_cast<double>(numChannels);
        for (double& sample : mono)
            sample *= scale;
    }
    return mono;
}

// Median, not mean, inter-beat-interval: robust against one or two
// spurious/missed beats at the edges of the detected grid. beatsInSteps are
// qm-dsp's own units (multiples of stepSize samples, per TempoTrackV2.h's
// "given in df increment units").
double medianInterBeatIntervalSeconds(const std::vector<double>& beatsInSteps, double sampleRate)
{
    std::vector<double> intervalsSeconds;
    intervalsSeconds.reserve(beatsInSteps.size());
    for (size_t i = 1; i < beatsInSteps.size(); ++i)
        intervalsSeconds.push_back((beatsInSteps[i] - beatsInSteps[i - 1]) * stepSize / sampleRate);

    std::sort(intervalsSeconds.begin(), intervalsSeconds.end());
    const size_t n = intervalsSeconds.size();
    if (n % 2 == 1)
        return intervalsSeconds[n / 2];
    return 0.5 * (intervalsSeconds[n / 2 - 1] + intervalsSeconds[n / 2]);
}

} // namespace

BeatGrid QmDspBeatDetector::analyze(const LoadedAudio& audio)
{
    if (audio.sampleRate <= 0.0 || audio.buffer.getNumSamples() <= 0 || audio.buffer.getNumChannels() <= 0)
        return BeatGrid{};

    const std::vector<double> mono = mixToMonoDouble(audio.buffer);
    const int numSamples = static_cast<int>(mono.size());

    DFConfig config{};
    config.stepSize = stepSize;
    config.frameLength = frameLength;
    config.DFType = DF_COMPLEXSD; // same detection function qm-vamp-plugins' BeatTrack.cpp defaults to
    config.dbRise = 3;
    config.adaptiveWhitening = false;
    config.whiteningRelaxCoeff = -1; // negative = qm-dsp picks its own sensible default
    config.whiteningFloor = -1;

    DetectionFunction detectionFunction(config);

    // Feed one frameLength-sample window at a time, hopped by stepSize
    // (overlapping, since frameLength == 2 * stepSize), zero-padding the
    // final partial window past the end of the buffer.
    std::vector<double> detectionValues;
    std::vector<double> window(static_cast<size_t>(frameLength), 0.0);
    for (int start = 0; start < numSamples; start += stepSize)
    {
        for (int i = 0; i < frameLength; ++i)
        {
            const int index = start + i;
            window[static_cast<size_t>(i)] = (index < numSamples) ? mono[static_cast<size_t>(index)] : 0.0;
        }
        detectionValues.push_back(detectionFunction.processTimeDomain(window.data()));
    }

    // TempoTrackV2::calculateBeatPeriod writes into its beatPeriod out-param
    // by index (inside viterbi_decode) without ever resizing it itself, so
    // the caller must pre-size it to match the detection-function vector,
    // zero-filled - the same precondition qm-vamp-plugins' BeatTrack.cpp (the
    // reference consumer of this class pair) relies on. Not documented in
    // TempoTrackV2.h itself; found by reading TempoTrackV2.cpp directly.
    std::vector<double> beatPeriod(detectionValues.size(), 0.0);
    std::vector<double> tempi;
    TempoTrackV2 tempoTracker(static_cast<float>(audio.sampleRate), stepSize);
    tempoTracker.calculateBeatPeriod(detectionValues, beatPeriod, tempi);

    std::vector<double> beats; // beat positions, in stepSize-sample units
    tempoTracker.calculateBeats(detectionValues, beatPeriod, beats);

    if (beats.size() < 2)
        return BeatGrid{}; // too short/silent to derive an interval

    BeatGrid grid;
    grid.firstBeatSeconds = beats[0] * stepSize / audio.sampleRate;
    const double medianIbi = medianInterBeatIntervalSeconds(beats, audio.sampleRate);
    grid.bpm = medianIbi > 0.0 ? 60.0 / medianIbi : 0.0;
    return grid;
}

} // namespace djapp
