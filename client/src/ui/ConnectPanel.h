#pragma once

// ui/ — ConnectPanel: url/room/name entry, connect/disconnect, claim/release
// control, and the peer list. Dumb view per 04-client.md: emits user intent via
// callbacks, renders whatever state the composition root pushes into it via the
// setters below; it never talks to SyncTransport/StateManager directly.

#include "model/Types.h"
#include "sync/SyncTransport.h" // ConnectionInfo
#include "sync/SyncPublisher.h" // Role
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace djapp
{

class ConnectPanel : public juce::Component
{
  public:
    struct PeerInfo { juce::String clientId, name; Role role; };

    ConnectPanel();

    // User-intent callbacks; set once by the composition root.
    std::function<void(const ConnectionInfo&)> onConnectRequested;
    std::function<void()> onDisconnectRequested;
    std::function<void()> onClaimRequested;
    std::function<void()> onReleaseRequested;

    // State setters; called by the composition root whenever transport/role state changes.
    void setConnectionStatus(bool connected, const juce::String& statusText);
    void setRole(Role role);
    void setPeers(std::vector<PeerInfo> peers);

    void resized() override;

  private:
    void onConnectButtonClicked();
    void onClaimButtonClicked();
    void refreshEnablement();
    void refreshPeerListDisplay();

    bool connected_ = false;
    Role role_ = Role::observer;
    std::vector<PeerInfo> peers_;

    juce::TextEditor urlField_, roomField_, nameField_;
    juce::Label urlLabel_, roomLabel_, nameLabel_;
    juce::TextButton connectButton_{"Connect"};
    juce::TextButton claimButton_{"Claim Control"};
    juce::Label statusLabel_;
    juce::TextEditor peerListDisplay_; // read-only, multi-line — "keep UI dumb" per 04-client.md,
                                       // a full juce::ListBoxModel is more machinery than a
                                       // read-only display needs
};

}
