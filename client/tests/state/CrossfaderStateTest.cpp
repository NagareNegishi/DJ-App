#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "state/CrossfaderState.h"

#include <utility>
#include <vector>

using namespace Catch::Matchers;
using namespace djapp;

TEST_CASE("a freshly constructed CrossfaderState defaults to center position", "[CrossfaderState]")
{
    CrossfaderState state;
    REQUIRE_THAT(state.getPosition(), WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("setPosition then getPosition returns the value that was set", "[CrossfaderState]")
{
    CrossfaderState state;
    state.setPosition(0.2f);
    REQUIRE_THAT(state.getPosition(), WithinAbs(0.2f, 1e-5f));
}

TEST_CASE("setPosition below zero clamps to zero", "[CrossfaderState]")
{
    CrossfaderState state;
    state.setPosition(-1.0f);
    REQUIRE_THAT(state.getPosition(), WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("setPosition above one clamps to one", "[CrossfaderState]")
{
    CrossfaderState state;
    state.setPosition(2.0f);
    REQUIRE_THAT(state.getPosition(), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("a listener is notified exactly once when setPosition changes the value", "[CrossfaderState]")
{
    CrossfaderState state;
    int callCount = 0;
    float lastValue = -1.0f;

    state.addListener([&](float position) {
        ++callCount;
        lastValue = position;
    });

    state.setPosition(0.3f);

    REQUIRE(callCount == 1);
    REQUIRE_THAT(lastValue, WithinAbs(0.3f, 1e-5f));
}

TEST_CASE("a listener is not notified when setPosition is called with the current value", "[CrossfaderState]")
{
    CrossfaderState state;
    state.setPosition(0.4f);

    int callCount = 0;
    state.addListener([&](float) { ++callCount; });

    state.setPosition(0.4f);

    REQUIRE(callCount == 0);
}

TEST_CASE("multiple listeners all fire exactly once with the same new value", "[CrossfaderState]")
{
    CrossfaderState state;
    int firstCallCount = 0;
    int secondCallCount = 0;
    float firstValue = -1.0f;
    float secondValue = -1.0f;

    state.addListener([&](float position) {
        ++firstCallCount;
        firstValue = position;
    });
    state.addListener([&](float position) {
        ++secondCallCount;
        secondValue = position;
    });

    state.setPosition(0.7f);

    REQUIRE(firstCallCount == 1);
    REQUIRE(secondCallCount == 1);
    REQUIRE_THAT(firstValue, WithinAbs(0.7f, 1e-5f));
    REQUIRE_THAT(secondValue, WithinAbs(0.7f, 1e-5f));
}

TEST_CASE("removeListener stops further notifications to that listener", "[CrossfaderState]")
{
    CrossfaderState state;
    int callCount = 0;

    int token = state.addListener([&](float) { ++callCount; });
    state.removeListener(token);

    state.setPosition(0.9f);

    REQUIRE(callCount == 0);
}

TEST_CASE("removeListener does not affect notifications to other listeners", "[CrossfaderState]")
{
    CrossfaderState state;
    int firstCallCount = 0;
    int secondCallCount = 0;

    int firstToken = state.addListener([&](float) { ++firstCallCount; });
    state.addListener([&](float) { ++secondCallCount; });

    state.removeListener(firstToken);
    state.setPosition(0.15f);

    REQUIRE(firstCallCount == 0);
    REQUIRE(secondCallCount == 1);
}

// --- White-box cases for the notification-walk internals, added after reading
// CrossfaderState.cpp directly. This class's file comment states "Listeners may
// add or remove registrations (including their own) from within their callback;
// walk by token via fresh lookups each step" -- these cases exercise exactly
// that, per the documented prior bug of this shape in StateManager
// (docs/plan/DEVIATIONS.md, 2026-08-06). ---

TEST_CASE("CrossfaderState: a listener removing a not-yet-visited listener mid-notification "
          "skips the removed one without crashing",
          "[state][CrossfaderState][whitebox]")
{
    CrossfaderState crossfader;
    std::vector<int> firedOrder;

    int victimToken = -1;
    crossfader.addListener(
        [&](float)
        {
            firedOrder.push_back(1);
            crossfader.removeListener(victimToken);
        });
    victimToken = crossfader.addListener([&](float) { firedOrder.push_back(2); });
    crossfader.addListener([&](float) { firedOrder.push_back(3); });

    REQUIRE_NOTHROW(crossfader.setPosition(0.8f));

    // Erasing a node the walk hasn't reached yet is well-defined: the victim
    // never fires, the walk still reaches the listener registered after it.
    CHECK(firedOrder == std::vector<int>{1, 3});
}

TEST_CASE("CrossfaderState: a listener removing an already-visited listener mid-notification "
          "is a no-op, later listeners still fire",
          "[state][CrossfaderState][whitebox]")
{
    CrossfaderState crossfader;
    std::vector<int> firedOrder;

    int earlierToken = crossfader.addListener([&](float) { firedOrder.push_back(1); });
    crossfader.addListener(
        [&](float)
        {
            firedOrder.push_back(2);
            crossfader.removeListener(earlierToken); // already fired; removal here is inert this pass
        });
    crossfader.addListener([&](float) { firedOrder.push_back(3); });

    REQUIRE_NOTHROW(crossfader.setPosition(0.8f));
    CHECK(firedOrder == std::vector<int>{1, 2, 3});

    // Confirm the removal actually took effect for good on a later call.
    firedOrder.clear();
    crossfader.setPosition(0.2f);
    CHECK(firedOrder == std::vector<int>{2, 3});
}

TEST_CASE("CrossfaderState: a listener that removes its own registration mid-notification "
          "does not crash and does not fire again on a later setPosition",
          "[state][CrossfaderState][whitebox]")
{
    CrossfaderState crossfader;
    std::vector<int> firedOrder;

    int selfToken = -1;
    selfToken = crossfader.addListener(
        [&](float)
        {
            firedOrder.push_back(1);
            crossfader.removeListener(selfToken);
        });
    crossfader.addListener([&](float) { firedOrder.push_back(2); });
    crossfader.addListener([&](float) { firedOrder.push_back(3); });

    REQUIRE_NOTHROW(crossfader.setPosition(0.8f));
    CHECK(firedOrder == std::vector<int>{1, 2, 3});

    firedOrder.clear();
    crossfader.setPosition(0.1f);
    CHECK(firedOrder == std::vector<int>{2, 3});
}

TEST_CASE("CrossfaderState: a listener adding a new listener mid-notification reaches the new one "
          "in the same setPosition call, since fresh tokens are always higher and the walk "
          "re-reads the map on every step",
          "[state][CrossfaderState][whitebox]")
{
    CrossfaderState crossfader;
    std::vector<int> firedOrder;

    crossfader.addListener(
        [&](float)
        {
            firedOrder.push_back(1);
            crossfader.addListener([&](float) { firedOrder.push_back(99); });
        });
    crossfader.addListener([&](float) { firedOrder.push_back(2); });

    REQUIRE_NOTHROW(crossfader.setPosition(0.8f));

    // The newly-added listener's token is higher than both pre-existing tokens
    // (nextToken_ is monotonic), so it sorts after listener 2 in the walk and is
    // reached before this same setPosition call returns.
    CHECK(firedOrder == std::vector<int>{1, 2, 99});
}

TEST_CASE("CrossfaderState: a listener calling setPosition again re-entrantly terminates "
          "(no infinite recursion) and converges to the last-set position",
          "[state][CrossfaderState][whitebox]")
{
    CrossfaderState crossfader;
    std::vector<std::pair<int, float>> firedOrder;

    // Listener 0 fires exactly once with a genuinely new value (0.8 != 0.5) and
    // reacts by setting a different position (0.3). Since clamped == position_
    // check guards re-entry, this cannot recurse forever: the second time
    // listener 0's own token is reached (from the nested walk it triggers),
    // position_ is already 0.3, so the nested setPosition(0.3) call is a no-op.
    crossfader.addListener(
        [&](float p)
        {
            firedOrder.emplace_back(0, p);
            if (p > 0.5f) // guard: only recurse from the outer call, not the nested re-entry
                crossfader.setPosition(0.3f);
        });
    crossfader.addListener([&](float p) { firedOrder.emplace_back(1, p); });
    crossfader.addListener([&](float p) { firedOrder.emplace_back(2, p); });

    REQUIRE_NOTHROW(crossfader.setPosition(0.8f));

    // No infinite recursion or stack overflow: the call returns. Final state
    // converges to the last value actually set (the re-entrant 0.3), not the
    // outer call's 0.8.
    CHECK(crossfader.getPosition() == 0.3f);

    // Whitebox finding: listeners registered after the re-entrant one (tokens 1
    // and 2) each fire twice for this single outer setPosition(0.8) call -- once
    // from the nested setPosition(0.3) triggered inside listener 0's callback,
    // and again from the outer walk resuming afterwards, because the outer walk
    // reads the live position_ member fresh on each step rather than a value
    // snapshotted at the start of its own walk. See EngineAdapter/CrossfaderState
    // whitebox report for this milestone.
    int firedCountFor1 = 0;
    int firedCountFor2 = 0;
    for (const auto& [token, value] : firedOrder)
    {
        if (token == 1)
            ++firedCountFor1;
        if (token == 2)
            ++firedCountFor2;
    }
    CHECK(firedCountFor1 == 2);
    CHECK(firedCountFor2 == 2);
    // Both duplicate firings observe the same, already-settled value (0.3), so
    // this does not corrupt state -- just redundant notification.
    for (const auto& [token, value] : firedOrder)
    {
        if (token == 1 || token == 2)
            CHECK(value == 0.3f);
    }
}
