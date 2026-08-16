#pragma once

// sync/ — WebSocketTransport: the real network transport (M7), replacing NullTransport.
// Owns an ix::WebSocket. Pimpl: the header holds only a pointer to the (incomplete)
// Impl type plus the interface overrides, so it stays IXWebSocket-free regardless of
// who includes it, without relying on every future reader remembering that rule by
// convention (MainComponent.h, the only consumer, is dj-app-client-only already).
//
// Threading: IXWebSocket's own network thread does nothing but parse-and-marshal;
// every Callbacks invocation crosses to the message thread via
// juce::MessageManager::callAsync (01-architecture.md's Threading model).

#include "SyncTransport.h"
#include <memory>

namespace djapp
{

class WebSocketTransport : public SyncTransport
{
  public:
    WebSocketTransport();
    ~WebSocketTransport() override;

    void connect(const ConnectionInfo&, Callbacks) override;
    void disconnect() override;
    void sendDelta(const StateDelta&) override;
    void sendClaimControl() override;
    void sendReleaseControl() override;
    void sendRequestSnapshot() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace djapp
