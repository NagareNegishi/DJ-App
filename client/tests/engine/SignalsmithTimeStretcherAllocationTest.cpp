// engine/ — required verification step named by docs/plan/10-beatsync-design.md's
// RT-safety section ("Required implementation-time task, not optional polish"):
// confirm SignalsmithTimeStretcher::process() allocates nothing once prepare()
// has run, at a representative spread of the app's real block sizes and
// numInput/numOutput ratios. See docs/plan/DEVIATIONS.md's 2026-08-22
// "Signalsmith Stretch v1.1.0 does not depend on Signalsmith Linear" entry:
// the real pre-sizing to verify is SignalsmithStretch::configure() (invoked
// by presetDefault(), invoked by prepare()), not a Linear::reserve() call
// that turns out not to exist on this pinned version's include path.
//
// Technique: override global operator new/delete with a thread-local counting
// flag, gated on by an RAII guard only around the process() calls under test.
// Counting stays off everywhere else in this binary (Catch2/JUCE startup and
// teardown, every other test file's own allocations, this file's own harness
// setup) so this override cannot turn an unrelated allocation anywhere else
// in dj-app-tests into a false failure here - the guard is the only thing
// that makes any allocation observable, and it's scoped to single process()
// calls with RAII, so it can't leak "on" past the block under test even if a
// process() call throws.

#include "engine/SignalsmithTimeStretcher.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace
{

// thread_local, not a plain global: if Catch2 ever runs test cases on a
// worker thread (it doesn't by default, but nothing in this file should rely
// on that), each thread's counting state stays independent rather than
// racing on a shared flag/counter.
thread_local bool g_countAllocations = false;
thread_local std::size_t g_allocationCount = 0;

// RAII: allocation counting is only ever "on" for the lifetime of one of
// these on the stack, so a process() call that throws still turns counting
// back off via the destructor rather than leaving it on for every later test
// in this binary.
struct AllocationCountGuard
{
    AllocationCountGuard()
    {
        g_allocationCount = 0;
        g_countAllocations = true;
    }
    ~AllocationCountGuard() { g_countAllocations = false; }

    AllocationCountGuard(const AllocationCountGuard&) = delete;
    AllocationCountGuard& operator=(const AllocationCountGuard&) = delete;
};

} // namespace

// Global operator new/delete overrides, scoped to this translation unit's
// linkage into dj-app-tests (the whole binary, since these are global, not
// class-scoped - see this file's header comment for why that is safe here).
// Routed through std::malloc/std::free directly rather than the previous
// global operator new/delete, so this file doesn't need to chain to
// :: operator new itself (which would just be the same default
// implementation anyway on every platform this project targets).
void* operator new(std::size_t size)
{
    if (g_countAllocations)
        ++g_allocationCount;
    void* ptr = std::malloc(size == 0 ? 1 : size);
    if (ptr == nullptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void* operator new[](std::size_t size)
{
    if (g_countAllocations)
        ++g_allocationCount;
    void* ptr = std::malloc(size == 0 ? 1 : size);
    if (ptr == nullptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}
void operator delete[](void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

namespace
{

// Fills a buffer with a cheap, nonzero, non-degenerate signal - real content
// so process() exercises its normal (not silence-fast-path) code path, not
// its "totalEnergy < noiseFloor" branch (see signalsmith-stretch.h's
// process(), the silence special-case near its top) which takes a
// deliberately different route through the code than the one this test's
// allocation claim is actually about.
void fillWithTone(std::vector<float>& buffer, double sampleRate, double frequencyHz, double phaseStartRadians)
{
    const double phaseIncrement = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = 0.5f * static_cast<float>(std::sin(phaseStartRadians + phaseIncrement * static_cast<double>(i)));
}

} // namespace

TEST_CASE("SignalsmithTimeStretcher::process() performs zero heap allocations once prepare() has run, "
          "across a representative spread of block sizes and tempo ratios",
          "[engine][TimeStretcher][SignalsmithTimeStretcher][allocation][rt-safety]")
{
    constexpr double sampleRate = 44100.0;

    djapp::SignalsmithTimeStretcher stretcher;

    // prepare()/presetDefault()/configure() are explicitly allowed to
    // allocate (TimeStretcher.h: "May allocate/reserve") - counting is off
    // here on purpose, this call is not part of the claim under test.
    stretcher.prepare(2, sampleRate);
    stretcher.setPitchSemitones(0.0f);
    stretcher.reset();

    // The real device buffer size is host-configured, not fixed in code (see
    // AudioDeviceHub / BufferPlaybackSource::prepareToPlay), so this sweeps a
    // spread of plausible sizes rather than pinning one number.
    const std::vector<int> blockSizes = {64, 128, 256, 512, 1024, 2048};

    // BufferPlaybackSource::getNextAudioBlock computes numInput = round(numOutput
    // * rate_) (see its step 5 comment); rate_ is clamped to
    // [playbackRateMin, playbackRateMax] = [0.5, 2.0] (model/Ranges.h). 0.5x
    // and 2.0x below are exactly that range's extremes, plus 1.0x (unity).
    const std::vector<double> rateMultipliers = {0.5, 1.0, 2.0};

    // Scratch buffers sized for the largest numInput this sweep will ever
    // request (2048 output frames * 2.0 rate = 4096 input frames), allocated
    // and filled with real content up front, outside the counting guard -
    // this is test-harness setup, not part of the SignalsmithTimeStretcher
    // contract being verified.
    constexpr int maxFrames = 2048 * 2 + 64; // headroom above the largest numInput actually used
    std::vector<float> inputLeft(static_cast<size_t>(maxFrames));
    std::vector<float> inputRight(static_cast<size_t>(maxFrames));
    fillWithTone(inputLeft, sampleRate, 440.0, 0.0);
    fillWithTone(inputRight, sampleRate, 440.0, 1.234); // distinct phase: exercises both channels independently
    std::vector<float> outputLeft(static_cast<size_t>(maxFrames));
    std::vector<float> outputRight(static_cast<size_t>(maxFrames));

    std::size_t totalAllocationsObserved = 0;
    std::size_t totalProcessCalls = 0;

    for (int numOutput : blockSizes)
    {
        for (double rateMultiplier : rateMultipliers)
        {
            const int numInput =
                std::max(1, static_cast<int>(std::lround(static_cast<double>(numOutput) * rateMultiplier)));
            REQUIRE(numInput <= maxFrames);
            REQUIRE(numOutput <= maxFrames);

            const float* inChannels[2] = {inputLeft.data(), inputRight.data()};
            float* outChannels[2] = {outputLeft.data(), outputRight.data()};

            {
                // Counting is on for exactly this one process() call - the
                // narrowest possible scope, so a failure here can only mean
                // process() itself allocated, not prepare()/reset()/anything
                // else in the harness around it.
                AllocationCountGuard guard;
                stretcher.process(inChannels, numInput, outChannels, numOutput);
                totalAllocationsObserved += g_allocationCount;
            }
            ++totalProcessCalls;

            // Every call must independently hold - report the specific
            // (numInput, numOutput) pair that failed, not just a final tally,
            // so a Finding (if any) names exactly where the allocation
            // happened.
            INFO("numInput=" << numInput << " numOutput=" << numOutput << " rateMultiplier=" << rateMultiplier);
            CHECK(g_allocationCount == 0);
        }
    }

    REQUIRE(totalProcessCalls == blockSizes.size() * rateMultipliers.size());
    CHECK(totalAllocationsObserved == 0);
}

TEST_CASE("SignalsmithTimeStretcher::process() performs zero heap allocations across many consecutive "
          "calls at a fixed block size, including calls that cross an internal STFT hop boundary",
          "[engine][TimeStretcher][SignalsmithTimeStretcher][allocation][rt-safety]")
{
    // Complements the sweep above (which checks breadth of block sizes /
    // ratios) with depth: a long steady-state run at one fixed size, the
    // shape a real playing deck actually produces block after block. If
    // configure()'s pre-sizing were only "big enough for the first call" but
    // something inside process() grows lazily on a later call (e.g. on first
    // crossing an STFT hop boundary), a single-call check could miss it.
    constexpr double sampleRate = 44100.0;
    constexpr int numOutput = 512; // AudioDeviceHub-typical block size
    constexpr int numInput = 512;  // rate 1.0

    djapp::SignalsmithTimeStretcher stretcher;
    stretcher.prepare(2, sampleRate);
    stretcher.setPitchSemitones(0.0f);
    stretcher.reset();

    std::vector<float> inputLeft(static_cast<size_t>(numInput));
    std::vector<float> inputRight(static_cast<size_t>(numInput));
    std::vector<float> outputLeft(static_cast<size_t>(numOutput));
    std::vector<float> outputRight(static_cast<size_t>(numOutput));

    constexpr int numCalls = 200; // several times over any plausible internal hop/window size

    for (int call = 0; call < numCalls; ++call)
    {
        // Distinct phase per call: real, non-repeating content, matching a
        // real playing track rather than a suspiciously-identical buffer.
        fillWithTone(inputLeft, sampleRate, 440.0, static_cast<double>(call));
        fillWithTone(inputRight, sampleRate, 440.0, static_cast<double>(call) + 0.5);

        const float* inChannels[2] = {inputLeft.data(), inputRight.data()};
        float* outChannels[2] = {outputLeft.data(), outputRight.data()};

        // Guard is scoped to exactly the process() call, same reasoning as
        // the sweep test above: Catch2's own INFO/CHECK machinery below
        // allocates internally (e.g. Catch::ScopedMessage's std::string),
        // and must run AFTER counting is back off, or it would be measuring
        // Catch2's bookkeeping instead of process().
        std::size_t allocationsThisCall = 0;
        {
            AllocationCountGuard guard;
            stretcher.process(inChannels, numInput, outChannels, numOutput);
            allocationsThisCall = g_allocationCount;
        }
        INFO("call=" << call);
        CHECK(allocationsThisCall == 0);
    }
}
