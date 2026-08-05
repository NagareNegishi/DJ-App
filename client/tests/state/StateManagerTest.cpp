#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "state/StateManager.h"

namespace djapp {

namespace {

StateDelta makeDelta(DeckId deck) {
    StateDelta delta;
    delta.deck = deck;
    return delta;
}

} // namespace

TEST_CASE("StateManager drops an empty delta without notifying or mutating state", "[state][StateManager]") {
    StateManager manager;

    int notifyCount = 0;
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { ++notifyCount; });

    auto before = manager.getState(DeckId::A);

    StateDelta empty = makeDelta(DeckId::A);
    REQUIRE(empty.empty());

    manager.applyDelta(empty, DeltaSource::local);

    CHECK(notifyCount == 0);
    auto after = manager.getState(DeckId::A);
    CHECK(after.gain == before.gain);
    CHECK(after.playbackRate == before.playbackRate);
    CHECK(after.positionSeconds == before.positionSeconds);
    CHECK(after.playing == before.playing);
    CHECK(after.trackId == before.trackId);
}

TEST_CASE("StateManager merge only changes fields present in the delta", "[state][StateManager]") {
    StateManager manager;

    StateDelta first = makeDelta(DeckId::A);
    first.trackId = std::string("track-1");
    first.gain = 1.2;
    first.playbackRate = 1.5;
    first.positionSeconds = 10.0;
    first.playing = false;
    manager.applyDelta(first, DeltaSource::local);

    StateDelta second = makeDelta(DeckId::A);
    second.gain = 0.8f;
    manager.applyDelta(second, DeltaSource::local);

    auto state = manager.getState(DeckId::A);
    // gain is float; compare against a float literal, not a double one, to avoid a
    // float->double promotion mismatch (float(0.8f) promoted to double != double 0.8).
    CHECK(state.gain == 0.8f);
    CHECK(state.playbackRate == 1.5);
    CHECK(state.positionSeconds == 10.0);
    CHECK(state.playing == false);
    CHECK(state.trackId == "track-1");
}

TEST_CASE("StateManager clamps out-of-range fields before merging", "[state][StateManager]") {
    StateManager manager;

    SECTION("gain above the maximum clamps to 2.0") {
        StateDelta delta = makeDelta(DeckId::A);
        delta.gain = 5.0;
        manager.applyDelta(delta, DeltaSource::local);
        CHECK(manager.getState(DeckId::A).gain == 2.0);
    }

    SECTION("playbackRate below the minimum clamps to 0.5") {
        StateDelta delta = makeDelta(DeckId::A);
        delta.playbackRate = 0.1;
        manager.applyDelta(delta, DeltaSource::local);
        CHECK(manager.getState(DeckId::A).playbackRate == 0.5);
    }
}

TEST_CASE("StateManager loop field follows double-optional merge semantics", "[state][StateManager]") {
    StateManager manager;

    SECTION("outer absent leaves a previously-set loop untouched") {
        StateDelta setLoop = makeDelta(DeckId::A);
        setLoop.loop = LoopPoints{10.0, 20.0};
        manager.applyDelta(setLoop, DeltaSource::local);
        REQUIRE(manager.getState(DeckId::A).loop.has_value());

        StateDelta unrelated = makeDelta(DeckId::A);
        unrelated.gain = 0.9; // loop left absent on this delta
        manager.applyDelta(unrelated, DeltaSource::local);

        CHECK(manager.getState(DeckId::A).loop.has_value());
    }

    SECTION("outer present, inner nullopt clears loop") {
        StateDelta setLoop = makeDelta(DeckId::A);
        setLoop.loop = LoopPoints{10.0, 20.0};
        manager.applyDelta(setLoop, DeltaSource::local);
        REQUIRE(manager.getState(DeckId::A).loop.has_value());

        StateDelta clear = makeDelta(DeckId::A);
        clear.loop = std::optional<LoopPoints>{std::nullopt};
        manager.applyDelta(clear, DeltaSource::local);

        CHECK_FALSE(manager.getState(DeckId::A).loop.has_value());
    }

    SECTION("outer present, inner value sets loop") {
        StateDelta setLoop = makeDelta(DeckId::A);
        setLoop.loop = LoopPoints{10.0, 20.0};
        manager.applyDelta(setLoop, DeltaSource::local);

        CHECK(manager.getState(DeckId::A).loop.has_value());
    }
}

TEST_CASE("StateManager injects the deck's current position into a local playing:true delta with no position",
          "[state][StateManager]") {
    StateManager manager;

    StateDelta prime = makeDelta(DeckId::A);
    prime.positionSeconds = 42.0;
    manager.applyDelta(prime, DeltaSource::local);
    REQUIRE(manager.getState(DeckId::A).positionSeconds == 42.0);

    std::optional<double> observedDeltaPosition;
    manager.addListener([&](const StateDelta& applied, const PlaybackState&, DeltaSource) {
        observedDeltaPosition = applied.positionSeconds;
    });

    StateDelta play = makeDelta(DeckId::A);
    play.playing = true;
    manager.applyDelta(play, DeltaSource::local);

    REQUIRE(observedDeltaPosition.has_value());
    CHECK(*observedDeltaPosition == 42.0);
    CHECK(manager.getState(DeckId::A).positionSeconds == 42.0);
}

TEST_CASE("StateManager does not inject position for a remote playing:true delta", "[state][StateManager]") {
    StateManager manager;

    StateDelta prime = makeDelta(DeckId::A);
    prime.positionSeconds = 42.0;
    manager.applyDelta(prime, DeltaSource::local);

    bool notified = false;
    bool observedHasPosition = false;
    manager.addListener([&](const StateDelta& applied, const PlaybackState&, DeltaSource) {
        notified = true;
        observedHasPosition = applied.positionSeconds.has_value();
    });

    StateDelta play = makeDelta(DeckId::A);
    play.playing = true;
    manager.applyDelta(play, DeltaSource::remote);

    REQUIRE(notified);
    CHECK_FALSE(observedHasPosition);
    // stored position is unaffected since positionSeconds stayed absent through the merge
    CHECK(manager.getState(DeckId::A).positionSeconds == 42.0);
}

TEST_CASE("StateManager tags each notification with the source passed to applyDelta", "[state][StateManager]") {
    StateManager manager;

    std::vector<DeltaSource> observed;
    manager.addListener(
        [&](const StateDelta&, const PlaybackState&, DeltaSource source) { observed.push_back(source); });

    StateDelta local = makeDelta(DeckId::A);
    local.gain = 1.0;
    manager.applyDelta(local, DeltaSource::local);

    StateDelta remote = makeDelta(DeckId::A);
    remote.gain = 1.1;
    manager.applyDelta(remote, DeltaSource::remote);

    REQUIRE(observed.size() == 2);
    CHECK(observed[0] == DeltaSource::local);
    CHECK(observed[1] == DeltaSource::remote);
}

TEST_CASE("StateManager notifies listeners in registration order", "[state][StateManager]") {
    StateManager manager;
    std::vector<int> firedOrder;

    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(1); });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(2); });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(3); });

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0;
    manager.applyDelta(delta, DeltaSource::local);

    CHECK(firedOrder == std::vector<int>{1, 2, 3});
}

TEST_CASE("StateManager stops notifying a removed listener", "[state][StateManager]") {
    StateManager manager;
    std::vector<int> firedOrder;

    int firstToken =
        manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(1); });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(2); });

    manager.removeListener(firstToken);

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0;
    manager.applyDelta(delta, DeltaSource::local);

    CHECK(firedOrder == std::vector<int>{2});
}

// --- White-box cases below, added after reading StateManager.cpp directly. ---
// The black-box suite above covers the documented contract (merge, clamp, injection,
// ordering, removal). These cases pin internals only visible from the notification
// loop's actual implementation: a token-based walk over listeners_ (find()/
// upper_bound() per step, not a live map iterator held across a callback -- see the
// fix-history comment below), and `nextToken_` never resetting.

TEST_CASE("StateManager token allocation: a removed token is never reused by a later addListener",
          "[state][StateManager][whitebox]") {
    StateManager manager;

    int first = manager.addListener([](const StateDelta&, const PlaybackState&, DeltaSource) {});
    int second = manager.addListener([](const StateDelta&, const PlaybackState&, DeltaSource) {});
    manager.removeListener(first);
    manager.removeListener(second);

    // Header comment: "Token starts at 0, increases monotonically, never reused."
    int third = manager.addListener([](const StateDelta&, const PlaybackState&, DeltaSource) {});
    CHECK(third != first);
    CHECK(third != second);
    CHECK(third > second);
}

TEST_CASE("StateManager: registering the same listener twice yields two independent tokens",
          "[state][StateManager][whitebox]") {
    StateManager manager;
    int sharedCount = 0;
    auto countingListener = [&](const StateDelta&, const PlaybackState&, DeltaSource) { ++sharedCount; };

    int tokenA = manager.addListener(countingListener);
    int tokenB = manager.addListener(countingListener);
    CHECK(tokenA != tokenB);

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0f;
    manager.applyDelta(delta, DeltaSource::local);

    // Both registrations of the identical listener fired independently.
    CHECK(sharedCount == 2);

    manager.removeListener(tokenA);
    manager.applyDelta(delta, DeltaSource::local);

    // Removing one registration leaves the other's independent token still firing.
    CHECK(sharedCount == 3);
}

TEST_CASE("StateManager re-entrancy: a listener applying a delta on a different deck does not corrupt "
          "the outer notification loop",
          "[state][StateManager][whitebox]") {
    StateManager manager;
    std::vector<std::string> firedOrder;

    // Listener 1 (deck A delta) nests a call to applyDelta for deck B from inside its
    // own notification callback -- a second, independent traversal of the same
    // listeners_ map while the outer traversal is still paused mid-iteration.
    manager.addListener([&](const StateDelta& applied, const PlaybackState&, DeltaSource) {
        firedOrder.push_back("1:" + toString(applied.deck).toStdString());
        if (applied.deck == DeckId::A) {
            StateDelta nested = makeDelta(DeckId::B);
            nested.gain = 1.0f;
            manager.applyDelta(nested, DeltaSource::local);
        }
    });
    manager.addListener(
        [&](const StateDelta& applied, const PlaybackState&, DeltaSource) { firedOrder.push_back("2:" + toString(applied.deck).toStdString()); });

    StateDelta outer = makeDelta(DeckId::A);
    outer.gain = 0.5f;
    manager.applyDelta(outer, DeltaSource::local);

    // The nested deck-B call runs to completion (both listeners see it) before the
    // outer loop resumes and reaches listener 2 for the original deck-A delta.
    REQUIRE(firedOrder.size() == 4);
    CHECK(firedOrder[0] == "1:A");
    CHECK(firedOrder[1] == "1:B");
    CHECK(firedOrder[2] == "2:B");
    CHECK(firedOrder[3] == "2:A");
}

TEST_CASE("StateManager re-entrancy: a listener registered from inside a notification callback "
          "also fires for that same in-flight delta",
          "[state][StateManager][whitebox]") {
    // Documents an actual internal consequence of iterating listeners_ (a std::map keyed
    // by monotonically increasing token) with a plain range-based for: addListener's new
    // token always sorts after every token visited so far, so std::map's insert does not
    // invalidate the range-for's cached end() iterator, and the new node lies on the
    // remaining traversal path. Nothing in StateManager's public contract promises or
    // forbids this either way -- pinned here as observed behavior, not a bug.
    StateManager manager;
    int lateListenerFireCount = 0;

    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) {
        manager.addListener(
            [&](const StateDelta&, const PlaybackState&, DeltaSource) { ++lateListenerFireCount; });
    });

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0f;
    manager.applyDelta(delta, DeltaSource::local);

    CHECK(lateListenerFireCount == 1);
}

TEST_CASE("StateManager re-entrancy: a listener removing a not-yet-visited listener during notification "
          "skips the removed one and still reaches the listener after it",
          "[state][StateManager][whitebox]") {
    StateManager manager;
    std::vector<int> firedOrder;

    int victimToken = -1;

    // Registration order: remover (1), victim (2, not yet fired when removed), trailing (3).
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) {
        firedOrder.push_back(1);
        manager.removeListener(victimToken);
    });
    victimToken = manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(2); });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(3); });

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0f;
    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));

    // Erasing a node the range-for hasn't reached yet is well-defined for std::map
    // (only the erased element's own iterator is invalidated): the victim never fires,
    // and the loop still reaches the listener registered after it.
    CHECK(firedOrder == std::vector<int>{1, 3});
}

TEST_CASE("StateManager re-entrancy: a listener that removes its own registration mid-notification "
          "does not crash and does not fire again",
          "[state][StateManager][whitebox]") {
    // Regression test for a real bug found via white-box testing: applyDelta's
    // notification loop used to be a raw range-for over listeners_ itself, so a
    // listener removing its own token from inside its callback invalidated the
    // range-for's iterator (reproduced as a real SIGSEGV). Fixed by walking tokens
    // via fresh find()/upper_bound() lookups instead of holding a map iterator
    // across a callback -- this test exercises exactly the crashing sequence.
    StateManager manager;
    std::vector<int> firedOrder;

    int selfToken = -1;
    selfToken = manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) {
        firedOrder.push_back(1);
        manager.removeListener(selfToken);
    });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(2); });
    manager.addListener([&](const StateDelta&, const PlaybackState&, DeltaSource) { firedOrder.push_back(3); });

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.0f;
    REQUIRE_NOTHROW(manager.applyDelta(delta, DeltaSource::local));

    CHECK(firedOrder == std::vector<int>{1, 2, 3});

    // The self-removed listener must not fire on a later delta either.
    firedOrder.clear();
    StateDelta second = makeDelta(DeckId::A);
    second.gain = 1.1f;
    manager.applyDelta(second, DeltaSource::local);
    CHECK(firedOrder == std::vector<int>{2, 3});
}

TEST_CASE("StateManager applies clamp to a delta before the local playing:true position-injection check, "
          "so an out-of-range explicit position is clamped rather than overwritten by injection",
          "[state][StateManager][whitebox]") {
    StateManager manager;

    StateDelta prime = makeDelta(DeckId::A);
    prime.positionSeconds = 42.0;
    manager.applyDelta(prime, DeltaSource::local);

    std::optional<double> observedDeltaPosition;
    manager.addListener([&](const StateDelta& applied, const PlaybackState&, DeltaSource) {
        observedDeltaPosition = applied.positionSeconds;
    });

    StateDelta play = makeDelta(DeckId::A);
    play.positionSeconds = -5.0; // out of range: clamps to 0.0 (ranges::clamp runs first)
    play.playing = true;
    manager.applyDelta(play, DeltaSource::local);

    REQUIRE(observedDeltaPosition.has_value());
    // Had injection run before clamp (or had clamp not run at all), the observed value
    // would be either the raw -5.0 or the previously-stored 42.0, not the clamped 0.0.
    CHECK(*observedDeltaPosition == 0.0);
    CHECK(manager.getState(DeckId::A).positionSeconds == 0.0);
}

TEST_CASE("StateManager applyDelta clears an invalid loop (in >= out after clamping) instead of merging "
          "a degenerate loop",
          "[state][StateManager][whitebox]") {
    StateManager manager;

    StateDelta setLoop = makeDelta(DeckId::A);
    setLoop.loop = LoopPoints{10.0, 20.0};
    manager.applyDelta(setLoop, DeltaSource::local);
    REQUIRE(manager.getState(DeckId::A).loop.has_value());

    // Both endpoints clamp to the same maximum -> ranges::clamp collapses the inner
    // optional to nullopt before StateManager ever merges it, so this delta clears the
    // previously-set loop rather than leaving it untouched or rejecting the delta.
    StateDelta invalid = makeDelta(DeckId::A);
    invalid.loop = LoopPoints{90000.0, 100000.0};
    manager.applyDelta(invalid, DeltaSource::local);

    CHECK_FALSE(manager.getState(DeckId::A).loop.has_value());
}

TEST_CASE("StateManager keeps deck A and deck B independent", "[state][StateManager]") {
    StateManager manager;

    auto beforeB = manager.getState(DeckId::B);
    CHECK_FALSE(beforeB.loop.has_value());
    CHECK(beforeB.playing == false);

    StateDelta delta = makeDelta(DeckId::A);
    delta.gain = 1.9;
    delta.playing = true;
    manager.applyDelta(delta, DeltaSource::local);

    auto afterB = manager.getState(DeckId::B);
    CHECK(afterB.gain == beforeB.gain);
    CHECK(afterB.playbackRate == beforeB.playbackRate);
    CHECK(afterB.positionSeconds == beforeB.positionSeconds);
    CHECK(afterB.playing == beforeB.playing);
    CHECK(afterB.trackId == beforeB.trackId);
}

} // namespace djapp
