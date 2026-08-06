// Black-box tests for djapp::SyncPublisher, derived from
// client/src/sync/SyncPublisher.h and the SyncPublisher spec (M4).
//
// SyncPublisher listens to a real StateManager and forwards a delta to a SyncTransport
// only when all of: the delta's source is DeltaSource::local, connected_ is true, and
// role_ is Role::controller. Defaults are connected = false, role = observer, so
// nothing forwards until both setters are called. DeltaSource::remote deltas must
// never be forwarded under any connected/role combination (anti-echo-loop rule).
//
// NOTE (spec gap, see report): the spec for this unit does not define the fields,
// constructibility, or comparison operators of StateDelta/PlaybackState/DeckId, only
// that they are the types used by StateManager::Listener. These tests therefore use a
// single default-constructed StateDelta{} as "a delta" and verify forwarding via
// call-count on a hand-written SyncTransport test double, rather than asserting deep
// structural equality of forwarded delta content (which would require knowing
// StateDelta's fields/operator==, not specified anywhere available to this task).

#include <catch2/catch_test_macros.hpp>

#include "state/StateManager.h"
#include "sync/SyncPublisher.h"
#include "sync/SyncTransport.h"

#include <juce_core/juce_core.h>

#include <optional>

using namespace djapp;

namespace
{

// StateManager::applyDelta drops an empty StateDelta before it ever reaches listeners
// (see state/StateManager.h), so "a delta" for these tests must have at least one
// field set — an all-default StateDelta{} would never reach SyncPublisher at all.
StateDelta nonEmptyDelta()
{
    StateDelta delta;
    delta.gain = 1.0f;
    return delta;
}

// Hand-written SyncTransport test double: records calls, never does real I/O.
class FakeTransport : public SyncTransport
{
  public:
    void connect(const ConnectionInfo&, Callbacks callbacks) override
    {
        connectCallCount += 1;
        capturedCallbacks = std::move(callbacks);
    }

    void disconnect() override { disconnectCallCount += 1; }

    void sendDelta(const StateDelta& delta) override
    {
        sendDeltaCallCount += 1;
        lastDelta = delta;
    }

    void sendClaimControl() override { claimControlCallCount += 1; }
    void sendReleaseControl() override { releaseControlCallCount += 1; }
    void sendRequestSnapshot() override { requestSnapshotCallCount += 1; }

    int connectCallCount = 0;
    int disconnectCallCount = 0;
    int sendDeltaCallCount = 0;
    int claimControlCallCount = 0;
    int releaseControlCallCount = 0;
    int requestSnapshotCallCount = 0;
    std::optional<StateDelta> lastDelta;
    std::optional<Callbacks> capturedCallbacks;
};

} // namespace

TEST_CASE("SyncPublisher does not forward while both connected and role are at their defaults")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    // Freshly constructed: connected_ = false, role_ = observer (both gates closed).
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == 0);
}

TEST_CASE("SyncPublisher does not forward when only setConnected(true) has been called")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    // role_ is still the default observer: role gate remains closed.
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == 0);
}

TEST_CASE("SyncPublisher does not forward when only setRole(controller) has been called")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setRole(Role::controller);
    // connected_ is still the default false: connected gate remains closed.
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == 0);
}

TEST_CASE("SyncPublisher forwards a local delta once both connected and controller gates are open")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == 1);
    REQUIRE(transport.lastDelta.has_value());
}

TEST_CASE("SyncPublisher never forwards a remote-sourced delta, even with both gates open")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::remote);

    // Anti-echo-loop rule: remote deltas are never forwarded, regardless of gate state.
    REQUIRE(transport.sendDeltaCallCount == 0);
}

TEST_CASE("SyncPublisher never forwards a remote-sourced delta when gates are closed either")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::remote);

    REQUIRE(transport.sendDeltaCallCount == 0);
}

TEST_CASE("SyncPublisher stops forwarding again after setConnected(false) closes a previously open gate")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 1);

    publisher.setConnected(false);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    // Call count must not have increased: forwarding stopped once connected_ went false.
    REQUIRE(transport.sendDeltaCallCount == 1);
}

TEST_CASE("SyncPublisher stops forwarding again after setRole(observer) closes a previously open gate")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 1);

    publisher.setRole(Role::observer);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == 1);
}

TEST_CASE("Destroying a SyncPublisher unregisters its StateManager listener")
{
    StateManager stateManager;
    FakeTransport transport;

    {
        SyncPublisher publisher(stateManager, transport);
        publisher.setConnected(true);
        publisher.setRole(Role::controller);

        stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
        REQUIRE(transport.sendDeltaCallCount == 1);
    }
    // publisher is now destroyed; its StateManager listener must have been removed.

    REQUIRE_NOTHROW(stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local));

    // No further forwarding calls should reach the transport once the publisher that
    // owned the gate state and listener registration no longer exists.
    REQUIRE(transport.sendDeltaCallCount == 1);
}

// --- White-box cases below, added after reading SyncPublisher.cpp directly. ---
// SyncPublisher's gate check (`source != local || !connected_ || role_ != controller`)
// reads connected_/role_ by reference to `this` from inside the listener closure, not a
// value captured at addListener time -- these cases confirm each notification re-reads
// the *current* member state rather than any snapshot.

TEST_CASE("SyncPublisher gate re-reads current connected/role state on every notification, "
          "not a stale snapshot from an earlier setter call")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 1);

    // Flip both gates closed, then back open, entirely between two applyDelta calls --
    // the third call must forward again, proving the check isn't cached anywhere.
    publisher.setRole(Role::observer);
    publisher.setConnected(false);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 1);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 2);
}

TEST_CASE("SyncPublisher forwards only the deltas sent while both gates happen to be open, "
          "across rapid interleaved toggling and applyDelta calls")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    int expectedForwards = 0;

    // deltas 1: closed (defaults)
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    // delta 2: connected only
    publisher.setConnected(true);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    // delta 3: connected + controller -> open
    publisher.setRole(Role::controller);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    ++expectedForwards;

    // delta 4: role flipped back to observer mid-stream -> closed again
    publisher.setRole(Role::observer);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    // delta 5: controller again, still connected -> open
    publisher.setRole(Role::controller);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    ++expectedForwards;

    // delta 6: disconnected mid-stream, role still controller -> closed
    publisher.setConnected(false);
    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(transport.sendDeltaCallCount == expectedForwards);
}

TEST_CASE("SyncPublisher gate reads the DeltaSource parameter fresh per call, not a value "
          "fixed at listener-registration time -- alternating local/remote forwards only the locals")
{
    StateManager stateManager;
    FakeTransport transport;
    SyncPublisher publisher(stateManager, transport);

    publisher.setConnected(true);
    publisher.setRole(Role::controller);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 1);

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::remote);
    REQUIRE(transport.sendDeltaCallCount == 1); // gates open, but remote source still blocks

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);
    REQUIRE(transport.sendDeltaCallCount == 2); // local resumes forwarding immediately after
}

TEST_CASE("A fresh SyncPublisher registers a listener independently of construction order")
{
    StateManager stateManager;
    FakeTransport firstTransport;
    FakeTransport secondTransport;

    SyncPublisher first(stateManager, firstTransport);
    first.setConnected(true);
    first.setRole(Role::controller);

    {
        SyncPublisher second(stateManager, secondTransport);
        second.setConnected(true);
        second.setRole(Role::controller);

        stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

        REQUIRE(firstTransport.sendDeltaCallCount == 1);
        REQUIRE(secondTransport.sendDeltaCallCount == 1);
    }
    // second is destroyed here; first must be unaffected and keep forwarding.

    stateManager.applyDelta(nonEmptyDelta(), DeltaSource::local);

    REQUIRE(firstTransport.sendDeltaCallCount == 2);
    REQUIRE(secondTransport.sendDeltaCallCount == 1);
}
