#pragma once

// sync/ — network boundary: carries StateDelta and control messages between
// this client and the sync server. SyncTransport is the interface consumed by
// state/app code; implementations own all wire I/O and thread marshalling.
//
// Threading: callers use it from the message thread. Implementations may run
// I/O on another thread but must marshal every Callbacks invocation onto the
// message thread themselves (see WebSocketTransport, M7).

#include "model/Types.h"
#include <functional>
#include <juce_core/juce_core.h>

namespace djapp
{

struct ConnectionInfo
{
    juce::String url, roomCode, displayName;
};

class SyncTransport
{
  public:
    virtual ~SyncTransport() = default;

    struct Callbacks // ALL invoked on the message thread (impl marshals)
    {
        std::function<void(const juce::var& welcome)> onWelcome;
        std::function<void(const StateDelta&)> onRemoteDelta;
        std::function<void(const juce::var& msg)> onServerEvent; // roleChanged/peerJoined/peerLeft/error/snapshot
        std::function<void(bool connected, juce::String reason)> onConnectionChange;
    };

    virtual void connect(const ConnectionInfo&, Callbacks) = 0;
    virtual void disconnect() = 0;
    virtual void sendDelta(const StateDelta&) = 0;
    virtual void sendClaimControl() = 0;
    virtual void sendReleaseControl() = 0;
    virtual void sendRequestSnapshot() = 0;
};

} // namespace djapp
