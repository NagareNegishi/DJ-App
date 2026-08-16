// Pins PositionClock's controller-only periodic resync emit: emitResyncNow() is the
// directly-callable stand-in for the real 5 s juce::Timer (see PositionClock.h).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "model/Types.h"
#include "state/PositionClock.h"
#include "state/StateManager.h"
#include "sync/SyncPublisher.h"

using namespace djapp;

namespace
{

// PositionClock only ever calls getCurrentPosition(); the rest are no-op stubs
// required to make FakeAudioEngine a concrete AudioEngine.
class FakeAudioEngine : public AudioEngine
{
  public:
    void load(std::shared_ptr<const LoadedAudio>) override {}
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void setGain(float) override {}
    void setPlaybackRate(float) override {}
    void setLoop(std::optional<LoopPoints>) override {}
    bool isPlaying() const override { return false; }

    double getCurrentPosition() const override
    {
        ++getCurrentPositionCallCount;
        return position;
    }

    double position = 0.0;
    mutable int getCurrentPositionCallCount = 0;
};

constexpr DeckId kThisDeck = DeckId::A;
constexpr DeckId kOtherDeck = DeckId::B;

StateDelta playingDelta(DeckId deck, bool playing)
{
    StateDelta delta;
    delta.deck = deck;
    delta.playing = playing;
    return delta;
}

} // namespace

TEST_CASE("PositionClock emits a positionSeconds resync when controller and playing",
          "[state][PositionClock]")
{
    StateManager stateManager;
    FakeAudioEngine engine;
    PositionClock clock(stateManager, engine, kThisDeck);

    stateManager.applyDelta(playingDelta(kThisDeck, true), DeltaSource::local);
    clock.setRole(Role::controller);
    engine.position = 42.5;

    clock.emitResyncNow();

    REQUIRE(engine.getCurrentPositionCallCount == 1);
    REQUIRE(stateManager.getState(kThisDeck).positionSeconds == Catch::Approx(42.5));
    // The emitted delta must carry positionSeconds only — playing must survive
    // untouched by the resync.
    REQUIRE(stateManager.getState(kThisDeck).playing == true);
}

TEST_CASE("PositionClock does nothing when observer even while playing",
          "[state][PositionClock]")
{
    StateManager stateManager;
    FakeAudioEngine engine;
    PositionClock clock(stateManager, engine, kThisDeck);

    stateManager.applyDelta(playingDelta(kThisDeck, true), DeltaSource::local);
    // role_ defaults to Role::observer; never claimed control.
    engine.position = 99.0;

    clock.emitResyncNow();

    REQUIRE(engine.getCurrentPositionCallCount == 0);
    REQUIRE(stateManager.getState(kThisDeck).positionSeconds != Catch::Approx(99.0));
}

TEST_CASE("PositionClock does nothing when controller but paused", "[state][PositionClock]")
{
    StateManager stateManager;
    FakeAudioEngine engine;
    PositionClock clock(stateManager, engine, kThisDeck);

    clock.setRole(Role::controller);
    // Deck is left at its default (not playing) state; no playing delta applied.
    engine.position = 7.0;

    clock.emitResyncNow();

    REQUIRE(engine.getCurrentPositionCallCount == 0);
    REQUIRE(stateManager.getState(kThisDeck).positionSeconds != Catch::Approx(7.0));
}

TEST_CASE("PositionClock::setRole takes effect immediately without waiting for "
          "another state notification",
          "[state][PositionClock]")
{
    // emitResyncNow()'s own guard (role_ == controller && playing) is the only
    // publicly observable proxy for "the flip took effect immediately": PositionClock
    // privately inherits juce::Timer, so isTimerRunning() is not reachable from a
    // test. This case pins the eligibility outcome, which is the closest available
    // stand-in for "claiming control while already playing starts the resync right
    // away" (see test-writer report finding on timer-state observability).
    StateManager stateManager;
    FakeAudioEngine engine;
    PositionClock clock(stateManager, engine, kThisDeck);

    stateManager.applyDelta(playingDelta(kThisDeck, true), DeltaSource::local);

    clock.emitResyncNow();
    REQUIRE(engine.getCurrentPositionCallCount == 0); // still observer, no-op

    // No further StateManager notification occurs between setRole and the next
    // emitResyncNow() call.
    clock.setRole(Role::controller);
    engine.position = 12.25;
    clock.emitResyncNow();

    REQUIRE(engine.getCurrentPositionCallCount == 1);
    REQUIRE(stateManager.getState(kThisDeck).positionSeconds == Catch::Approx(12.25));
}

TEST_CASE("PositionClock ignores playing changes on the other deck", "[state][PositionClock]")
{
    // Guards `applied.deck != deck_` in the listener. emitResyncNow() always
    // re-reads stateManager_.getState(deck_) directly, so the only externally
    // observable proxy for this guard is that a delta aimed at the other deck never
    // changes this deck's own resync eligibility in either direction.
    StateManager stateManager;
    FakeAudioEngine engine;
    PositionClock clock(stateManager, engine, kThisDeck);
    clock.setRole(Role::controller);

    SECTION("other deck starting to play does not make this (non-playing) deck emit")
    {
        stateManager.applyDelta(playingDelta(kOtherDeck, true), DeltaSource::local);

        clock.emitResyncNow();

        REQUIRE(engine.getCurrentPositionCallCount == 0);
        REQUIRE(stateManager.getState(kThisDeck).playing == false);
    }

    SECTION("other deck stopping does not stop this (already playing) deck's eligibility")
    {
        stateManager.applyDelta(playingDelta(kThisDeck, true), DeltaSource::local);
        stateManager.applyDelta(playingDelta(kOtherDeck, false), DeltaSource::local);
        engine.position = 3.5;

        clock.emitResyncNow();

        REQUIRE(engine.getCurrentPositionCallCount == 1);
        REQUIRE(stateManager.getState(kThisDeck).positionSeconds == Catch::Approx(3.5));
    }
}

TEST_CASE("PositionClock unregisters its listener on destruction without crashing",
          "[state][PositionClock]")
{
    StateManager stateManager;
    FakeAudioEngine engine;

    {
        PositionClock clock(stateManager, engine, kThisDeck);
        clock.setRole(Role::controller);
    }

    // If the destructor failed to call removeListener, this would notify a dangling
    // listener.
    stateManager.applyDelta(playingDelta(kThisDeck, true), DeltaSource::local);
    stateManager.applyDelta(playingDelta(kThisDeck, false), DeltaSource::local);

    SUCCEED("no crash after the clock that owned the listener was destroyed");
}
