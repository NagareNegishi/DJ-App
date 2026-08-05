#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/EngineAdapter.h"
#include "repository/AudioRepository.h"
#include "state/StateManager.h"

namespace djapp {

namespace {

class FakeAudioEngine final : public AudioEngine {
public:
    void load(std::shared_ptr<const LoadedAudio> audio) override {
        calls.push_back("load");
        lastLoadedAudio = std::move(audio);
    }

    void seek(double positionSeconds) override {
        calls.push_back("seek");
        lastSeekPosition = positionSeconds;
    }

    void setGain(float gain) override {
        calls.push_back("setGain");
        lastGain = gain;
    }

    void setPlaybackRate(float rate) override {
        calls.push_back("setPlaybackRate");
        lastPlaybackRate = rate;
    }

    void setLoop(std::optional<LoopPoints> loop) override {
        calls.push_back("setLoop");
        setLoopCalled = true;
        lastLoop = loop;
    }

    void play() override { calls.push_back("play"); }
    void pause() override { calls.push_back("pause"); }

    double getCurrentPosition() const override { return 0.0; }
    bool isPlaying() const override { return false; }

    std::vector<std::string> calls;
    std::shared_ptr<const LoadedAudio> lastLoadedAudio;
    double lastSeekPosition = 0.0;
    float lastGain = 0.0f;
    float lastPlaybackRate = 0.0f;
    bool setLoopCalled = false;
    std::optional<LoopPoints> lastLoop;
};

class FakeAudioRepository final : public AudioRepository {
public:
    std::vector<TrackMetadata> listAvailableTracks() override { return {}; }

    std::optional<TrackMetadata> getTrackMetadata(const juce::String&) override { return std::nullopt; }

    std::shared_ptr<const LoadedAudio> getAudioBuffer(const juce::String& trackId) override {
        auto it = buffers.find(trackId);
        return it == buffers.end() ? nullptr : it->second;
    }

    std::map<juce::String, std::shared_ptr<const LoadedAudio>> buffers;
};

StateDelta makeDelta(DeckId deck) {
    StateDelta delta;
    delta.deck = deck;
    return delta;
}

} // namespace

TEST_CASE("EngineAdapter maps a gain-only delta to setGain alone", "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter loads the buffer the repository resolves for a present trackId", "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter skips the load and does not crash when the repository resolves nullptr",
          "[engine][EngineAdapter]") {
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository; // "missing-track" intentionally absent from the map

    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string("missing-track");

    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));
    CHECK(engine.calls.empty());
}

TEST_CASE("EngineAdapter makes no engine call and does not crash for an explicit-empty trackId",
          "[engine][EngineAdapter]") {
    StateManager manager;
    FakeAudioEngine engine;
    FakeAudioRepository repository;
    EngineAdapter adapter(manager, DeckId::A, engine, repository);

    StateDelta delta = makeDelta(DeckId::A);
    delta.trackId = std::string();

    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));
    CHECK(engine.calls.empty());
}

TEST_CASE("EngineAdapter loads before seeking when both trackId and positionSeconds are present",
          "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter calls play() last, after gain and playbackRate in the same delta",
          "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter ignores deltas for the other deck", "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter passes nullopt to setLoop for an explicit loop clear", "[engine][EngineAdapter]") {
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

TEST_CASE("EngineAdapter passes the loop value through to setLoop when present", "[engine][EngineAdapter]") {
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
          "[engine][EngineAdapter][whitebox]") {
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
          "[engine][EngineAdapter][whitebox]") {
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
          "[engine][EngineAdapter][whitebox]") {
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

} // namespace djapp
