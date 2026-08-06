#pragma once

// sync/ — NullTransport: the SyncTransport used before WebSocketTransport
// lands (M7). It never connects and never invokes any callback; it exists so
// the composition root and SyncPublisher have a concrete transport to build
// against. Threading: message-thread-only, same as the interface it implements.

#include "SyncTransport.h"

namespace djapp
{

class NullTransport : public SyncTransport
{
  public:
    void connect(const ConnectionInfo&, Callbacks) override;
    void disconnect() override;
    void sendDelta(const StateDelta&) override;
    void sendClaimControl() override;
    void sendReleaseControl() override;
    void sendRequestSnapshot() override;

  private:
    // Captured but never invoked — kept so a future test can assert connect()
    // was called with the callbacks it was given, without pretending to connect.
    Callbacks callbacks_;
};

} // namespace djapp
