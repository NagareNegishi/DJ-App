// Black-box tests for djapp::NullTransport, derived from
// client/src/sync/SyncTransport.h and the NullTransport spec (M4).
//
// NullTransport must be a fully inert stand-in for SyncTransport: every method is a
// no-op, connect() stores (but never fires) the Callbacks it receives, and no callback
// is ever invoked by any method call, in any order, including calls made before
// connect() or after disconnect().

#include <catch2/catch_test_macros.hpp>

#include "sync/NullTransport.h"
#include "sync/SyncTransport.h"
#include "state/StateManager.h"

#include <juce_core/juce_core.h>

using namespace djapp;

namespace {

ConnectionInfo makeConnectionInfo()
{
    ConnectionInfo info;
    info.url = "wss://example.test/ws";
    info.roomCode = "ABCD";
    info.displayName = "Tester";
    return info;
}

struct CallbackFlags
{
    bool welcomeCalled = false;
    bool remoteDeltaCalled = false;
    bool serverEventCalled = false;
    bool connectionChangeCalled = false;
};

SyncTransport::Callbacks makeCallbacks(CallbackFlags& flags)
{
    SyncTransport::Callbacks callbacks;
    callbacks.onWelcome = [&flags](const juce::var&) { flags.welcomeCalled = true; };
    callbacks.onRemoteDelta = [&flags](const StateDelta&) { flags.remoteDeltaCalled = true; };
    callbacks.onServerEvent = [&flags](const juce::var&) { flags.serverEventCalled = true; };
    callbacks.onConnectionChange = [&flags](bool, juce::String) { flags.connectionChangeCalled = true; };
    return callbacks;
}

bool anyCallbackFired(const CallbackFlags& flags)
{
    return flags.welcomeCalled || flags.remoteDeltaCalled || flags.serverEventCalled
           || flags.connectionChangeCalled;
}

} // namespace

TEST_CASE("NullTransport methods are no-ops callable before connect() without crashing")
{
    NullTransport transport;
    StateDelta delta {};

    SECTION("disconnect before connect")
    {
        REQUIRE_NOTHROW(transport.disconnect());
    }

    SECTION("sendDelta before connect")
    {
        REQUIRE_NOTHROW(transport.sendDelta(delta));
    }

    SECTION("sendClaimControl before connect")
    {
        REQUIRE_NOTHROW(transport.sendClaimControl());
    }

    SECTION("sendReleaseControl before connect")
    {
        REQUIRE_NOTHROW(transport.sendReleaseControl());
    }

    SECTION("sendRequestSnapshot before connect")
    {
        REQUIRE_NOTHROW(transport.sendRequestSnapshot());
    }

    SECTION("mixed order: send methods, disconnect, then connect, then send again")
    {
        REQUIRE_NOTHROW(transport.sendClaimControl());
        REQUIRE_NOTHROW(transport.sendReleaseControl());
        REQUIRE_NOTHROW(transport.sendRequestSnapshot());
        REQUIRE_NOTHROW(transport.sendDelta(delta));
        REQUIRE_NOTHROW(transport.disconnect());

        CallbackFlags flags;
        REQUIRE_NOTHROW(transport.connect(makeConnectionInfo(), makeCallbacks(flags)));

        REQUIRE_NOTHROW(transport.sendClaimControl());
        REQUIRE_NOTHROW(transport.sendReleaseControl());
        REQUIRE_NOTHROW(transport.sendRequestSnapshot());
        REQUIRE_NOTHROW(transport.sendDelta(delta));
        REQUIRE_NOTHROW(transport.disconnect());

        REQUIRE_FALSE(anyCallbackFired(flags));
    }

    SECTION("connect can be called multiple times without crashing")
    {
        CallbackFlags firstFlags;
        CallbackFlags secondFlags;
        REQUIRE_NOTHROW(transport.connect(makeConnectionInfo(), makeCallbacks(firstFlags)));
        REQUIRE_NOTHROW(transport.connect(makeConnectionInfo(), makeCallbacks(secondFlags)));

        REQUIRE_FALSE(anyCallbackFired(firstFlags));
        REQUIRE_FALSE(anyCallbackFired(secondFlags));
    }
}

TEST_CASE("NullTransport::connect stores the Callbacks without ever invoking them")
{
    NullTransport transport;
    CallbackFlags flags;

    transport.connect(makeConnectionInfo(), makeCallbacks(flags));

    // connect() itself must not fire onConnectionChange (or anything else) synchronously.
    REQUIRE_FALSE(anyCallbackFired(flags));

    StateDelta delta {};
    transport.sendDelta(delta);
    transport.sendClaimControl();
    transport.sendReleaseControl();
    transport.sendRequestSnapshot();
    transport.disconnect();

    // None of the other five methods may invoke any of the captured callbacks either.
    REQUIRE_FALSE(flags.welcomeCalled);
    REQUIRE_FALSE(flags.remoteDeltaCalled);
    REQUIRE_FALSE(flags.serverEventCalled);
    REQUIRE_FALSE(flags.connectionChangeCalled);
}

TEST_CASE("NullTransport::disconnect does not invoke onConnectionChange")
{
    NullTransport transport;
    CallbackFlags flags;
    transport.connect(makeConnectionInfo(), makeCallbacks(flags));

    transport.disconnect();

    // NullTransport "reports disconnected" by never claiming to be connected in the
    // first place; disconnect() must not actively announce disconnection either.
    REQUIRE_FALSE(flags.connectionChangeCalled);
}

TEST_CASE("NullTransport with no connect() call at all still allows every method to be called safely")
{
    NullTransport transport;
    StateDelta delta {};

    REQUIRE_NOTHROW(transport.sendDelta(delta));
    REQUIRE_NOTHROW(transport.sendClaimControl());
    REQUIRE_NOTHROW(transport.sendReleaseControl());
    REQUIRE_NOTHROW(transport.sendRequestSnapshot());
    REQUIRE_NOTHROW(transport.disconnect());
}
