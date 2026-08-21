#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/EngineAdapter.h"
#include "model/CrossfaderCurve.h"
#include "repository/AudioRepository.h"
#include "state/CrossfaderState.h"
#include "state/StateManager.h"

namespace djapp
{

namespace
{

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

} // namespace

TEST_CASE("EngineAdapter maps a gain-only delta to setGain alone", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.3f;
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.calls.size() == 1);
    CHECK(engine.calls[0] == "setGain");
    // lastGain is float; compare against a float literal to avoid a float->double
    // promotion mismatch (float(1.3f) promoted to double != double 1.3).
    CHECK(engine.lastGain == 1.3f);
}

TEST_CASE("EngineAdapter loads the buffer the repository resolves for a present trackId", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("track-1");
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.calls.size() == 1);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.lastLoadedAudio == buffer);
}

TEST_CASE("EngineAdapter loads a null buffer and does not crash when the repository resolves nullptr",
          "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository; // "missing-track" intentionally absent from the map

    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("missing-track");

    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));
    REQUIRE(engine.calls.size() == 1);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.lastLoadedAudio == nullptr);
}

TEST_CASE("EngineAdapter loads a null buffer and does not crash for an explicit-empty trackId",
          "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string();

    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));
    REQUIRE(engine.calls.size() == 1);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.lastLoadedAudio == nullptr);
}

TEST_CASE("EngineAdapter loads a null buffer instead of leaving the previous track playing when a "
          "later delta's trackId is unresolvable",
          "[engine][EngineAdapter][regression]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(engine.lastLoadedAudio == buffer);

    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string("missing-track"); // not in repository.buffers
    second.positionSeconds = 5.0;
    manager.applyDelta(second, DeltaSource::local);

    // The stale buffer must not survive the delta that failed to resolve a new one.
    CHECK(engine.lastLoadedAudio == nullptr);
    REQUIRE(engine.calls.size() == 3);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.calls[1] == "load");
    CHECK(engine.calls[2] == "seek");
}

TEST_CASE("EngineAdapter loads a null buffer instead of leaving the previous track playing when a "
          "later delta clears trackId",
          "[engine][EngineAdapter][regression]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    REQUIRE(engine.lastLoadedAudio == buffer);

    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string(); // explicit clear
    manager.applyDelta(second, DeltaSource::local);

    CHECK(engine.lastLoadedAudio == nullptr);
    REQUIRE(engine.calls.size() == 2);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.calls[1] == "load");
}

TEST_CASE("EngineAdapter loads before seeking when both trackId and positionSeconds are present",
          "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("track-1");
    delta.positionSeconds = 12.5;
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.calls.size() == 2);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.calls[1] == "seek");
    CHECK(engine.lastSeekPosition == 12.5);
}

TEST_CASE("EngineAdapter calls play() last, after gain and playbackRate in the same delta", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.1;
    delta.playbackRate = 1.2;
    delta.playing = true;

    // Applied as a remote delta deliberately: StateManager's position-injection rule
    // only fires for local playing:true deltas and would otherwise insert an extra
    // seek() call ahead of play() here, which is not what this test is pinning.
    manager.applyDelta(delta, DeltaSource::remote);

    REQUIRE(engine.calls.size() == 3);
    CHECK(engine.calls[0] == "setGain");
    CHECK(engine.calls[1] == "setPlaybackRate");
    CHECK(engine.calls[2] == "play");
}

TEST_CASE("EngineAdapter ignores deltas for the other deck", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::B);
    delta.gain = 1.5;
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);

    CHECK(engine.calls.empty());
}

TEST_CASE("EngineAdapter passes nullopt to setLoop for an explicit loop clear", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.loop = std::optional<LoopPoints>{std::nullopt};
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.setLoopCalled);
    CHECK_FALSE(engine.lastLoop.has_value());
}

TEST_CASE("EngineAdapter passes the loop value through to setLoop when present", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.loop = LoopPoints{10.0, 20.0};
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.setLoopCalled);
    CHECK(engine.lastLoop.has_value());
}

// --- White-box cases below, added after reading EngineAdapter.cpp directly. ---

TEST_CASE("EngineAdapter applies all six fields present in a single delta in the exact order "
          "load, seek, setGain, setPlaybackRate, setLoop, play",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("track-1");
    delta.positionSeconds = 30.0;
    delta.gain = 0.7f;
    delta.playbackRate = 1.4f;
    delta.loop = LoopPoints{5.0, 15.0};
    delta.playing = true;

    // Applied remotely so StateManager's local-only position-injection rule cannot
    // insert extra fields ahead of the explicit set already present on this delta.
    manager.applyDelta(delta, DeltaSource::remote);

    REQUIRE(engine.calls.size() == 6);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.calls[1] == "seek");
    CHECK(engine.calls[2] == "setGain");
    CHECK(engine.calls[3] == "setPlaybackRate");
    CHECK(engine.calls[4] == "setLoop");
    CHECK(engine.calls[5] == "play");
    CHECK(engine.lastLoadedAudio == buffer);
    CHECK(engine.lastSeekPosition == 30.0);
    CHECK(engine.lastGain == 0.7f);
    CHECK(engine.lastPlaybackRate == 1.4f);
    REQUIRE(engine.lastLoop.has_value());
    CHECK(engine.lastLoop->inSeconds == 5.0);
    CHECK(engine.lastLoop->outSeconds == 15.0);
}

TEST_CASE("EngineAdapter destructor removes only its own StateManager listener, "
          "leaving another EngineAdapter on the same StateManager unaffected",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engineOne;
    FakeAudioEngine engineTwo;
    FakeAudioRepository repository;

    auto adapterOne = std::make_unique<EngineAdapter>(manager, DeckId::A, engineOne, repository);
    EngineAdapter adapterTwo(manager, DeckId::A, engineTwo, repository);

    StateDelta before = makeDelta(DeckId::A);
    before.gain = 0.6f;
    manager.applyDelta(before, DeltaSource::local);
    REQUIRE(engineOne.calls.size() == 1);
    REQUIRE(engineTwo.calls.size() == 1);

    adapterOne.reset(); // destroys only adapterOne's listener registration

    StateDelta after = makeDelta(DeckId::A);
    after.gain = 0.9f;
    REQUIRE_NOTHROW(manager.applyDelta(after, DeltaSource::local));

    // adapterOne's engine received no further calls; adapterTwo's did.
    CHECK(engineOne.calls.size() == 1);
    CHECK(engineTwo.calls.size() == 2);
    CHECK(engineTwo.lastGain == 0.9f);
}

TEST_CASE("EngineAdapter re-resolves trackId through the repository on every delta that carries one, "
          "not just the first",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto bufferOne = std::make_shared<LoadedAudio>();
    auto bufferTwo = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = bufferOne;
    repository.buffers["track-2"] = bufferTwo;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    manager.applyDelta(first, DeltaSource::local);
    CHECK(engine.lastLoadedAudio == bufferOne);

    StateDelta second = makeDelta(DeckId::A);
    second.trackId = std::string("track-2");
    manager.applyDelta(second, DeltaSource::local);
    CHECK(engine.lastLoadedAudio == bufferTwo);

    REQUIRE(engine.calls.size() == 2);
    CHECK(engine.calls[0] == "load");
    CHECK(engine.calls[1] == "load");
}

TEST_CASE("EngineAdapter checkForSelfStop makes no correction while the engine is still playing",
          "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);
    REQUIRE(engine.playing);

    const std::size_t callsBeforeCheck = engine.calls.size();
    adapter.checkForSelfStop();

    CHECK(engine.calls.size() == callsBeforeCheck);
    CHECK(manager.getState(DeckId::A).playing);
}

TEST_CASE("EngineAdapter checkForSelfStop clears state.playing and re-invokes pause() when the "
          "engine stopped itself",
          "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);
    REQUIRE(engine.playing);

    // Flip the fake's flag directly rather than calling pause(): production
    // self-stop happens on the audio thread, which this test can't and
    // shouldn't spin up.
    engine.playing = false;

    adapter.checkForSelfStop();

    CHECK_FALSE(manager.getState(DeckId::A).playing);
    CHECK(engine.calls.back() == "pause");
}

TEST_CASE("EngineAdapter checkForSelfStop is a no-op for a deck that was never playing", "[engine][EngineAdapter]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    adapter.checkForSelfStop();

    CHECK(engine.calls.empty());
    CHECK_FALSE(manager.getState(DeckId::A).playing);
}

// --- Additional white-box cases for the self-stop correction. ---

TEST_CASE("EngineAdapter checkForSelfStop's corrective delta freezes positionSeconds rather than "
          "resetting it, per the design's no-positionSeconds decision",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    delta.positionSeconds = 42.0;
    manager.applyDelta(delta, DeltaSource::local);
    REQUIRE(manager.getState(DeckId::A).positionSeconds == 42.0);

    engine.playing = false; // simulate the audio thread's own self-stop
    adapter.checkForSelfStop();

    CHECK_FALSE(manager.getState(DeckId::A).playing);
    // Not reset to 0 and not re-derived from the engine: last known position stands.
    CHECK(manager.getState(DeckId::A).positionSeconds == 42.0);
}

TEST_CASE("EngineAdapter checkForSelfStop is idempotent: a second call after the corrective delta "
          "has already landed adds no further engine calls",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);

    engine.playing = false;
    adapter.checkForSelfStop();
    REQUIRE(engine.calls.back() == "pause");
    const std::size_t callsAfterFirstCorrection = engine.calls.size();

    // engine.playing is still false (nothing plays it back true), so state.playing
    // is also already false: the mismatch condition no longer holds.
    adapter.checkForSelfStop();
    adapter.checkForSelfStop();

    CHECK(engine.calls.size() == callsAfterFirstCorrection);
    CHECK_FALSE(manager.getState(DeckId::A).playing);
}

TEST_CASE("EngineAdapter runs through two independent play/self-stop cycles, correcting each one",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta playDelta = makeDelta(DeckId::A);
    playDelta.playing = true;

    manager.applyDelta(playDelta, DeltaSource::local);
    REQUIRE(engine.playing);
    engine.playing = false; // first self-stop
    adapter.checkForSelfStop();
    CHECK_FALSE(manager.getState(DeckId::A).playing);

    manager.applyDelta(playDelta, DeltaSource::local); // user hits Play again
    REQUIRE(engine.playing);
    REQUIRE(manager.getState(DeckId::A).playing);
    engine.playing = false; // second self-stop
    adapter.checkForSelfStop();
    CHECK_FALSE(manager.getState(DeckId::A).playing);

    // Each bare playDelta also carries an injected "seek" (StateManager's local
    // playing:true position-injection rule, since playDelta has no explicit
    // positionSeconds), interleaved ahead of the "play" it accompanies.
    REQUIRE(engine.calls.size() == 6);
    CHECK(engine.calls[0] == "seek");
    CHECK(engine.calls[1] == "play");
    CHECK(engine.calls[2] == "pause");
    CHECK(engine.calls[3] == "seek");
    CHECK(engine.calls[4] == "play");
    CHECK(engine.calls[5] == "pause");
}

TEST_CASE("EngineAdapter checkForSelfStop only ever touches its own deck's state and engine, "
          "leaving a sibling adapter on the other deck untouched",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    FakeAudioRepository repository;
    EngineAdapter adapterA(manager, DeckId::A, engineA, repository);
    EngineAdapter adapterB(manager, DeckId::B, engineB, repository);

    StateDelta playA = makeDelta(DeckId::A);
    playA.playing = true;
    StateDelta playB = makeDelta(DeckId::B);
    playB.playing = true;
    manager.applyDelta(playA, DeltaSource::local);
    manager.applyDelta(playB, DeltaSource::local);

    engineA.playing = false; // only deck A's engine self-stopped
    const std::size_t engineBCallsBefore = engineB.calls.size();

    adapterA.checkForSelfStop();

    CHECK_FALSE(manager.getState(DeckId::A).playing);
    CHECK(manager.getState(DeckId::B).playing); // untouched
    CHECK(engineB.calls.size() == engineBCallsBefore);
    CHECK(engineB.playing);
}

TEST_CASE("EngineAdapter checkForSelfStop corrects a remotely-applied playing:true the same way, "
          "and the corrective delta itself always carries DeltaSource::local",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    std::vector<DeltaSource> observedSources;
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource source)
                        { observedSources.push_back(source); });

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    delta.positionSeconds = 5.0; // remote deltas carrying playing:true must supply position themselves
    manager.applyDelta(delta, DeltaSource::remote);
    REQUIRE(engine.playing);

    engine.playing = false;
    adapter.checkForSelfStop();

    CHECK_FALSE(manager.getState(DeckId::A).playing);
    REQUIRE(observedSources.size() == 2);
    CHECK(observedSources[0] == DeltaSource::remote); // the original play
    CHECK(observedSources[1] == DeltaSource::local);  // this client's own correction, per spec
}

TEST_CASE("EngineAdapter checkForSelfStop's corrective delta carries only deck and playing, "
          "no other field",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta observed;
    bool observedAnything = false;
    manager.addListener(
        [&](const StateDelta& applied, const PlaybackState&, DeltaSource)
        {
            if (!applied.playing.has_value() || *applied.playing)
                return; // skip the initial playing:true delta below; capture only the correction
            observed = applied;
            observedAnything = true;
        });

    StateDelta delta = makeDelta(DeckId::A);
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);

    engine.playing = false;
    adapter.checkForSelfStop();

    REQUIRE(observedAnything);
    REQUIRE(observed.playing.has_value());
    CHECK_FALSE(*observed.playing);
    CHECK_FALSE(observed.trackId.has_value());
    CHECK_FALSE(observed.positionSeconds.has_value());
    CHECK_FALSE(observed.gain.has_value());
    CHECK_FALSE(observed.playbackRate.has_value());
    CHECK_FALSE(observed.pitchOffsetSemitones.has_value());
    CHECK_FALSE(observed.loop.has_value());
}

// --- White-box cases for attachCrossfader/pushEffectiveGain, added after
// reading EngineAdapter.h/.cpp and CrossfaderState.h/.cpp directly. ---

TEST_CASE("EngineAdapter attachCrossfader pushes the effective gain for the crossfader's actual "
          "current position, not the default center",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    CrossfaderState crossfader;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta gainDelta = makeDelta(DeckId::A);
    gainDelta.gain = 2.0f;
    manager.applyDelta(gainDelta, DeltaSource::local);
    REQUIRE(engine.lastGain == 2.0f);

    // Full-A position: gainA == 1.0, distinct from the default center's ~0.707,
    // so a wrong "always use 0.5" implementation would be caught here.
    crossfader.setPosition(0.0f);
    adapter.attachCrossfader(crossfader);

    CHECK(engine.calls.back() == "setGain");
    CHECK(engine.lastGain == 2.0f); // 2.0 * gainA(0.0) == 2.0 * 1.0
}

TEST_CASE("EngineAdapter maps DeckId::B to the crossfader's gainB, not gainA", "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    CrossfaderState crossfader;
    EngineAdapter adapter(manager, DeckId::B, engine, repository);

    // Full-B position: gainB == 1.0, gainA == 0.0. Deck B must observe gainB;
    // an accidental gainA read here would push ~0 instead of the full 1.0 *
    // state.gain(1.0 default) == 1.0.
    crossfader.setPosition(1.0f);
    adapter.attachCrossfader(crossfader);

    CHECK(engine.lastGain == 1.0f);
}

TEST_CASE("EngineAdapter: a track-load delta (trackId present, gain absent) does not touch gain, "
          "and the crossfader-composed gain still reads the post-load gain correctly on the next "
          "crossfader move -- it does not go stale or reset to 1.0",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    auto buffer = std::make_shared<LoadedAudio>();
    repository.buffers["track-1"] = buffer;
    CrossfaderState crossfader;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    crossfader.setPosition(0.0f); // full A: gainA == 1.0
    adapter.attachCrossfader(crossfader);
    REQUIRE(engine.lastGain == 1.0f); // 1.0 (default gain) * 1.0

    StateDelta gainDelta = makeDelta(DeckId::A);
    gainDelta.gain = 2.0f; // gainMax (Ranges.h): the highest value that survives clamping unchanged
    manager.applyDelta(gainDelta, DeltaSource::local);
    REQUIRE(engine.lastGain == 2.0f);

    const std::size_t callsBeforeLoad = engine.calls.size();
    StateDelta loadDelta = makeDelta(DeckId::A);
    loadDelta.trackId = std::string("track-1"); // no .gain field on this delta
    manager.applyDelta(loadDelta, DeltaSource::local);

    // Only "load" happened; the load delta itself must not touch gain.
    REQUIRE(engine.calls.size() == callsBeforeLoad + 1);
    CHECK(engine.calls.back() == "load");
    CHECK(engine.lastGain == 2.0f); // unchanged by the load

    // Move the crossfader to full-B: the multiplier must be recomputed against
    // the gain the track load left behind (2.0), not a stale or reset value.
    // Compare against equalPowerCrossfade's own output rather than a hardcoded
    // 0.0f literal: cos(pi/2) is not guaranteed to be bit-exact zero in float.
    crossfader.setPosition(1.0f);
    CHECK(engine.calls.back() == "setGain");
    CHECK(engine.lastGain == 2.0f * equalPowerCrossfade(1.0f).gainA);
}

TEST_CASE("EngineAdapter checkForSelfStop's corrective playing:false delta does not touch gain "
          "at all -- no setGain call, crossfader-composed gain unchanged",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    CrossfaderState crossfader;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    crossfader.setPosition(0.25f);
    adapter.attachCrossfader(crossfader);

    StateDelta gainDelta = makeDelta(DeckId::A);
    gainDelta.gain = 2.0f;
    manager.applyDelta(gainDelta, DeltaSource::local);
    const float gainAfterSet = engine.lastGain;

    StateDelta playDelta = makeDelta(DeckId::A);
    playDelta.playing = true;
    manager.applyDelta(playDelta, DeltaSource::local);
    REQUIRE(engine.playing);

    engine.playing = false; // simulate the audio thread's own self-stop
    adapter.checkForSelfStop();

    CHECK_FALSE(manager.getState(DeckId::A).playing);
    CHECK(engine.calls.back() == "pause");
    CHECK(engine.lastGain == gainAfterSet); // untouched by the correction
}

TEST_CASE("EngineAdapter attachCrossfader called twice (by mistake) on the same crossfader replaces "
          "the first listener registration instead of leaking it, so a single later crossfader "
          "move pushes gain exactly once",
          "[engine][EngineAdapter][whitebox]")
{
    // attachCrossfader removes any existing crossfader listener registration before
    // installing the new one, so a second attachCrossfader call on the same crossfader
    // leaves exactly one live registration (see EngineAdapter.cpp attachCrossfader).
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    CrossfaderState crossfader;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    adapter.attachCrossfader(crossfader);
    adapter.attachCrossfader(crossfader); // mistaken second attach

    const std::size_t callsBeforeMove = engine.calls.size();
    crossfader.setPosition(0.9f); // a single logical move

    std::size_t setGainCallsFromThisMove = 0;
    for (std::size_t i = callsBeforeMove; i < engine.calls.size(); ++i)
        if (engine.calls[i] == "setGain")
            ++setGainCallsFromThisMove;

    CHECK(setGainCallsFromThisMove == 1);
}

// --- White-box: handleDelta's repeat branch (M9). The FakeAudioEngine above already
// tracks setRepeatCalled/lastRepeat, but no existing test case ever sets delta.repeat
// and asserts on them -- these pin that handleDelta's `if (applied.repeat.has_value())`
// branch (EngineAdapter.cpp) actually reaches engine_.setRepeat with the right value. ---

TEST_CASE("EngineAdapter maps a repeat-only delta to setRepeat alone", "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.repeat = false;
    manager.applyDelta(delta, DeltaSource::local);

    REQUIRE(engine.calls.size() == 1);
    CHECK(engine.calls[0] == "setRepeat");
    CHECK(engine.setRepeatCalled);
    CHECK_FALSE(engine.lastRepeat);
}

TEST_CASE("EngineAdapter forwards both repeat values to setRepeat with the value the delta carried",
          "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta toFalse = makeDelta(DeckId::A);
    toFalse.repeat = false;
    manager.applyDelta(toFalse, DeltaSource::local);
    CHECK_FALSE(engine.lastRepeat);

    StateDelta toTrue = makeDelta(DeckId::A);
    toTrue.repeat = true;
    manager.applyDelta(toTrue, DeltaSource::local);
    CHECK(engine.lastRepeat);
}

TEST_CASE("EngineAdapter ignores a repeat delta aimed at the other deck", "[engine][EngineAdapter][whitebox]")
{
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository); // this adapter drives deck A only

    StateDelta delta = makeDelta(DeckId::B);
    delta.repeat = false;
    manager.applyDelta(delta, DeltaSource::local);

    CHECK(engine.calls.empty());
    CHECK_FALSE(engine.setRepeatCalled);
}

} // namespace djapp
