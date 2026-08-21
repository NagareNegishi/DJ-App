// Pins DeckPositionClocks::setRole fanning a single call out to both decks'
// PositionClocks - the direct regression test for the bug this class exists to
// close (a hand-duplicated per-deck call site that forgot one deck). Eligibility
// is proven via emitResyncNow(), the same directly-callable stand-in for the real
// 5 s juce::Timer used by PositionClockTest.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/AudioEngine.h"
#include "model/Types.h"
#include "state/DeckPositionClocks.h"
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

StateDelta playingDelta(DeckId deck, bool playing)
{
    StateDelta delta;
    delta.deck = deck;
    delta.playing = playing;
    return delta;
}

} // namespace

TEST_CASE("DeckPositionClocks::setRole(controller) makes both decks eligible to resync",
          "[state][DeckPositionClocks]")
{
    StateManager stateManager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    PositionClock clockA(stateManager, engineA, DeckId::A);
    PositionClock clockB(stateManager, engineB, DeckId::B);
    DeckPositionClocks deckClocks(clockA, clockB);

    stateManager.applyDelta(playingDelta(DeckId::A, true), DeltaSource::local);
    stateManager.applyDelta(playingDelta(DeckId::B, true), DeltaSource::local);

    // A single call through the wrapper, not two calls to the individual clocks.
    deckClocks.setRole(Role::controller);

    engineA.position = 10.0;
    engineB.position = 20.0;
    clockA.emitResyncNow();
    clockB.emitResyncNow();

    // A hand-duplicated call site that forgot deck B would leave clockB_ at
    // Role::observer, so getCurrentPositionCallCount would stay 0 for engineB.
    REQUIRE(engineA.getCurrentPositionCallCount == 1);
    REQUIRE(engineB.getCurrentPositionCallCount == 1);
    REQUIRE(stateManager.getState(DeckId::A).positionSeconds == Catch::Approx(10.0));
    REQUIRE(stateManager.getState(DeckId::B).positionSeconds == Catch::Approx(20.0));
}

TEST_CASE("DeckPositionClocks::setRole(observer) after controller disables resync eligibility on both decks",
          "[state][DeckPositionClocks]")
{
    StateManager stateManager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    PositionClock clockA(stateManager, engineA, DeckId::A);
    PositionClock clockB(stateManager, engineB, DeckId::B);
    DeckPositionClocks deckClocks(clockA, clockB);

    stateManager.applyDelta(playingDelta(DeckId::A, true), DeltaSource::local);
    stateManager.applyDelta(playingDelta(DeckId::B, true), DeltaSource::local);

    deckClocks.setRole(Role::controller);
    deckClocks.setRole(Role::observer);

    engineA.position = 99.0;
    engineB.position = 88.0;
    clockA.emitResyncNow();
    clockB.emitResyncNow();

    REQUIRE(engineA.getCurrentPositionCallCount == 0);
    REQUIRE(engineB.getCurrentPositionCallCount == 0);
    REQUIRE(stateManager.getState(DeckId::A).positionSeconds != Catch::Approx(99.0));
    REQUIRE(stateManager.getState(DeckId::B).positionSeconds != Catch::Approx(88.0));
}

TEST_CASE("a single DeckPositionClocks::setRole call reaches deck B specifically, not just deck A",
          "[state][DeckPositionClocks]")
{
    // Isolates the exact shape of the fixed bug: only deck B is playing here, so
    // if the wrapper's setRole silently dropped clockB_ (mirroring the
    // hand-duplicated call site that forgot deck B), deck B would never become
    // eligible and this assertion would fail regardless of deck A's state.
    StateManager stateManager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    PositionClock clockA(stateManager, engineA, DeckId::A);
    PositionClock clockB(stateManager, engineB, DeckId::B);
    DeckPositionClocks deckClocks(clockA, clockB);

    stateManager.applyDelta(playingDelta(DeckId::B, true), DeltaSource::local);

    deckClocks.setRole(Role::controller);

    engineB.position = 55.5;
    clockB.emitResyncNow();

    REQUIRE(engineB.getCurrentPositionCallCount == 1);
    REQUIRE(stateManager.getState(DeckId::B).positionSeconds == Catch::Approx(55.5));
}

TEST_CASE("constructing a DeckPositionClocks does not itself change either clock's role",
          "[state][DeckPositionClocks]")
{
    // The wrapper only forwards on an explicit setRole call; it must not claim
    // control (or otherwise touch role_) as a side effect of construction.
    StateManager stateManager;
    FakeAudioEngine engineA;
    FakeAudioEngine engineB;
    PositionClock clockA(stateManager, engineA, DeckId::A);
    PositionClock clockB(stateManager, engineB, DeckId::B);
    DeckPositionClocks deckClocks(clockA, clockB);
    (void)deckClocks;

    stateManager.applyDelta(playingDelta(DeckId::A, true), DeltaSource::local);
    stateManager.applyDelta(playingDelta(DeckId::B, true), DeltaSource::local);

    engineA.position = 1.0;
    engineB.position = 2.0;
    clockA.emitResyncNow();
    clockB.emitResyncNow();

    REQUIRE(engineA.getCurrentPositionCallCount == 0);
    REQUIRE(engineB.getCurrentPositionCallCount == 0);
}
