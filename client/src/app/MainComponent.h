#pragma once

// app/ — composition root. MainComponent owns repository/engine/state/sync
// wiring, growing as later milestones land. At M7 it owns the AudioRepository,
// the TrackListComponent that lists what it finds, one deck's worth of engine
// wiring, a DeckComponent that renders and drives that deck's StateManager
// state (never the engine directly), and the full sync stack: a real
// WebSocketTransport, SyncPublisher, PositionClock, and the ConnectPanel that
// surfaces connection/role/peer state and routes user intent back through this
// class's handle* methods.

#include "engine/AudioDeviceHub.h"
#include "engine/EngineAdapter.h"
#include "engine/JuceAudioEngine.h"
#include "repository/LocalFileRepository.h"
#include "state/PositionClock.h"
#include "state/StateManager.h"
#include "sync/SyncPublisher.h"
#include "sync/SyncTransport.h"
#include "ui/ConnectPanel.h"
#include "ui/DeckComponent.h"
#include "ui/TrackListComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace djapp
{

class MainComponent : public juce::Component
{
  public:
    MainComponent();

    void resized() override;

  private:
    double computeResumePositionSeconds();

    void handleConnectRequested(const ConnectionInfo& info);
    void handleWelcome(const juce::var& welcome);
    void handleRemoteDelta(const StateDelta& delta);
    void handleServerEvent(const juce::var& msg);
    void handleConnectionChange(bool connected, juce::String reason);
    void applyRoleToUI();
    bool canControlLocally() const;
    void applyDeckSnapshot(DeckId deck, const juce::var& playbackStateVar);
    void applySnapshot(const juce::var& snapshotDecksVar);

    LocalFileRepository repository_;
    TrackListComponent trackList_;

    AudioDeviceHub deviceHub_;
    JuceAudioEngine engineA_;

    StateManager stateManager_;
    EngineAdapter engineAdapterA_;
    PositionClock positionClock_;
    std::unique_ptr<SyncTransport> transport_;
    SyncPublisher syncPublisher_;

    ConnectPanel connectPanel_;
    DeckComponent deckA_;

    Role role_ = Role::observer;
    juce::String ownClientId_;
    bool connected_ = false;

    // One log line per classification per connection, reset on each new connect
    // transition — mirrors WebSocketTransport's own logOnce discipline one layer
    // down, so a hostile/broken server can't forge unbounded log lines via
    // repeated "error" frames or grow the client's log by spamming peerJoined
    // past the room's max size.
    bool loggedServerError_ = false;
    bool loggedPeerCapExceeded_ = false;

    // ConnectPanel is a dumb view with no getter for what it's currently
    // displaying; roleChanged/peerJoined/peerLeft need to read-modify-write
    // the peer list, so this class keeps the copy of record.
    std::vector<ConnectPanel::PeerInfo> peers_;
};

} // namespace djapp
