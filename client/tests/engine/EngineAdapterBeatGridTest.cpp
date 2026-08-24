// Black-box tests for the BeatDetector-backed BeatGrid caching added to
// EngineAdapter (see docs/plan/10-beatsync-design.md, "BeatDetector" ->
// "Threading and caching", and the unit spec deriving this file). Deliberately
// kept in its own file, separate from EngineAdapterTest.cpp, so the two
// suites merge cleanly regardless of which lands first.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/BeatDetector.h"
#include "engine/EngineAdapter.h"
#include "model/BeatGrid.h"
#include "repository/AudioRepository.h"
#include "state/StateManager.h"

namespace djapp
{

namespace
{

// --- Fakes duplicated from EngineAdapterTest.cpp. They live in that file's own
// anonymous namespace (not header-exported), so this file carries its own,
// behaviorally-identical copies rather than sharing a definition across
// translation units. Kept verbatim on purpose - do not diverge their behavior
// from the original. ---

class FakeAudioEngine final : public AudioEngine
{
  public:
    void load(std::shared_ptr<const LoadedAudio> audio) override
    {
        calls.push_back("load");
        lastLoadedAudio = std::move(audio);
    }

    void seek(double positionSeconds) override
    {
        calls.push_back("seek");
        lastSeekPosition = positionSeconds;
    }

    void setGain(float gain) override
    {
        calls.push_back("setGain");
        lastGain = gain;
    }

    void setPlaybackRate(float rate) override
    {
        calls.push_back("setPlaybackRate");
        lastPlaybackRate = rate;
    }

    void setPitchOffsetSemitones(float semitones) override
    {
        calls.push_back("setPitchOffsetSemitones");
        lastPitchOffsetSemitones = semitones;
    }

    void setLoop(std::optional<LoopPoints> loop) override
    {
        calls.push_back("setLoop");
        setLoopCalled = true;
        lastLoop = loop;
    }

    void setRepeat(bool repeat) override
    {
        calls.push_back("setRepeat");
        setRepeatCalled = true;
        lastRepeat = repeat;
    }

    void play() override
    {
        calls.push_back("play");
        playing = true;
    }
    void pause() override
    {
        calls.push_back("pause");
        playing = false;
    }

    double getCurrentPosition() const override { return 0.0; }
    bool isPlaying() const override { return playing; }

    std::vector<std::string> calls;
    std::shared_ptr<const LoadedAudio> lastLoadedAudio;
    double lastSeekPosition = 0.0;
    float lastGain = 0.0f;
    float lastPlaybackRate = 0.0f;
    float lastPitchOffsetSemitones = 0.0f;
    bool setLoopCalled = false;
    std::optional<LoopPoints> lastLoop;
    bool setRepeatCalled = false;
    bool lastRepeat = true;
    // Public so tests can flip it directly to simulate the audio thread's own
    // self-stop, which happens without a pause() call.
    bool playing = false;
};

class FakeAudioRepository final : public AudioRepository
{
  public:
    std::vector<TrackMetadata> listAvailableTracks() override { return {}; }

    std::optional<TrackMetadata> getTrackMetadata(const juce::String&) override { return std::nullopt; }

    std::shared_ptr<const LoadedAudio> getAudioBuffer(const juce::String& trackId) override
    {
        auto it = buffers.find(trackId);
        return it == buffers.end() ? nullptr : it->second;
    }

    std::map<juce::String, std::shared_ptr<const LoadedAudio>> buffers;
};

StateDelta makeDelta(DeckId deck)
{
    StateDelta delta;
    delta.deck = deck;
    return delta;
}

// New for this unit (per the spec's "Test impact" section): a BeatDetector
// double that returns a caller-configured BeatGrid and counts analyze()
// calls, so "did it re-analyze an already-loaded buffer" is directly
// observable - NullBeatDetector alone can't answer that, since it doesn't
// count calls.
class FakeBeatDetector final : public BeatDetector
{
  public:
    BeatGrid analyze(const LoadedAudio& audio) override
    {
        (void)audio; // the double doesn't inspect the buffer, only counts and returns `result`
        ++analyzeCallCount;
        return result;
    }

    BeatGrid result;
    int analyzeCallCount = 0;
};

} // namespace

TEST_CASE("EngineAdapter currentBeatGrid returns BeatGrid{} before attachBeatDetector is ever called, "
          "even after a track loads successfully",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    // No attachBeatDetector call anywhere in this test.
    CHECK(adapter.currentBeatGrid().bpm == 0.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.0);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("track-1");
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.lastLoadedAudio == buffer); // the load itself did succeed
    CHECK(adapter.currentBeatGrid().bpm == 0.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.0);
}

TEST_CASE("EngineAdapter currentBeatGrid returns the attached detector's analysis result after a "
          "resolvable track loads",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("track-1");
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(detector.analyzeCallCount == 1);
    CHECK(adapter.currentBeatGrid().bpm == 128.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.35);
}

TEST_CASE("EngineAdapter does not re-analyze when the same trackId is re-applied (same resolved "
          "buffer pointer)",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(detector.analyzeCallCount == 1);

    // FakeAudioRepository, like the real LocalFileRepository, returns the same
    // shared_ptr for a given track id across calls, so this re-selects the
    // exact same buffer pointer.
    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string("track-1");
    manager.applyDelta(second, DeltaSource::local);

    CHECK(detector.analyzeCallCount == 1); // no re-analysis
    CHECK(adapter.currentBeatGrid().bpm == 128.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.35);
}

TEST_CASE("EngineAdapter re-analyzes and updates currentBeatGrid when a different trackId (different "
          "buffer) loads",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto bufferOne = std::make_shared<LoadedAudio>();
    auto bufferTwo = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = bufferOne;
    repository.buffers["track-2"] = bufferTwo;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(detector.analyzeCallCount == 1);
    REQUIRE(adapter.currentBeatGrid().bpm == 128.0);

    detector.result = BeatGrid{95.0, 1.2}; // distinct result configured for the second buffer
    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string("track-2");
    manager.applyDelta(second, DeltaSource::local);

    CHECK(detector.analyzeCallCount == 2);
    CHECK(adapter.currentBeatGrid().bpm == 95.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 1.2);
}

TEST_CASE("EngineAdapter resets currentBeatGrid to BeatGrid{} when a trackId the repository can't "
          "resolve loads, even if a grid was already cached",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository; // "missing-track" intentionally absent from the map
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(adapter.currentBeatGrid().bpm == 128.0);

    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string("missing-track");
    manager.applyDelta(second, DeltaSource::local);

    CHECK(adapter.currentBeatGrid().bpm == 0.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.0);
    // A failed load must not call analyze() at all - only the reset happens.
    CHECK(detector.analyzeCallCount == 1);
}

TEST_CASE("EngineAdapter resets currentBeatGrid to BeatGrid{} on an explicit-empty trackId (track "
          "clear)",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(adapter.currentBeatGrid().bpm == 128.0);

    StateDelta clear = makeDelta(DeckId::A);
    clear.trackId = std::string(); // explicit clear
    manager.applyDelta(clear, DeltaSource::local);

    CHECK(adapter.currentBeatGrid().bpm == 0.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.0);
    CHECK(detector.analyzeCallCount == 1); // clearing never calls analyze()
}

TEST_CASE("EngineAdapter leaves currentBeatGrid untouched by a delta that carries no trackId at all",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);
    adapter.attachBeatDetector(detector);

    StateDelta load = makeDelta(DeckId::A);
    load.trackId = std::string("track-1");
    manager.applyDelta(load, DeltaSource::local);
    REQUIRE(adapter.currentBeatGrid().bpm == 128.0);
    REQUIRE(detector.analyzeCallCount == 1);

    StateDelta gainOnly = makeDelta(DeckId::A);
    gainOnly.gain = 1.5f; // no .trackId set on this delta
    manager.applyDelta(gainOnly, DeltaSource::local);

    CHECK(adapter.currentBeatGrid().bpm == 128.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.35);
    CHECK(detector.analyzeCallCount == 1); // untouched: no extra analyze() call
}

TEST_CASE("EngineAdapter instances for different decks keep independent BeatGrid caches",
          "[engine][EngineAdapter][beatgrid]")
{
    StateManager manager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    FakeAudioRepository repository;
    FakeBeatDetector detectorA;
    FakeBeatDetector detectorB;
    detectorA.result = BeatGrid{128.0, 0.35};
    detectorB.result = BeatGrid{95.0, 1.2};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapterA(manager, DeckId::A, engineA, repository);
    EngineAdapter adapterB(manager, DeckId::B, engineB, repository);
    adapterA.attachBeatDetector(detectorA);
    adapterB.attachBeatDetector(detectorB);

    StateDelta deltaA = makeDelta(DeckId::A);
    deltaA.trackId = std::string("track-1");
    manager.applyDelta(deltaA, DeltaSource::local);

    CHECK(adapterA.currentBeatGrid().bpm == 128.0);
    // Deck B never received a trackId delta, so its cache must still be default.
    CHECK(adapterB.currentBeatGrid().bpm == 0.0);
    CHECK(adapterB.currentBeatGrid().firstBeatSeconds == 0.0);
    CHECK(detectorB.analyzeCallCount == 0);
}

// --- Additional case pinning the spec's explicitly-documented "accepted
// limitation": attachBeatDetector does no immediate analysis of an
// already-loaded track. ---

TEST_CASE("EngineAdapter attachBeatDetector performs no immediate analysis for an already-loaded "
          "track; the cache stays default until the next trackId delta",
          "[engine][EngineAdapter][beatgrid][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    FakeBeatDetector detector;
    detector.result = BeatGrid{128.0, 0.35};
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    // Track loads before any detector is attached.
    StateDelta load = makeDelta(DeckId::A);
    load.trackId = std::string("track-1");
    manager.applyDelta(load, DeltaSource::local);
    REQUIRE(engine.lastLoadedAudio == buffer);
    REQUIRE(adapter.currentBeatGrid().bpm == 0.0);

    adapter.attachBeatDetector(detector);

    // attachBeatDetector itself must not trigger analysis of the already-loaded track.
    CHECK(adapter.currentBeatGrid().bpm == 0.0);
    CHECK(detector.analyzeCallCount == 0);

    // Only the *next* trackId delta (even re-selecting the same track) analyzes it.
    StateDelta reselect = makeDelta(DeckId::A);
    reselect.trackId = std::string("track-1");
    manager.applyDelta(reselect, DeltaSource::local);

    CHECK(detector.analyzeCallCount == 1);
    CHECK(adapter.currentBeatGrid().bpm == 128.0);
    CHECK(adapter.currentBeatGrid().firstBeatSeconds == 0.35);
}

} // namespace djapp
