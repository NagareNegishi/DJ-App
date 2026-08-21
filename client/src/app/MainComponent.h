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
#include "state/CrossfaderState.h"
#include "state/DeckPositionClocks.h"
#include "state/PositionClock.h"
#include "state/StateManager.h"
#include "sync/SyncPublisher.h"
#include "sync/SyncTransport.h"
#include "ui/ConnectPanel.h"
#include "ui/DeckComponent.h"
#include "ui/MixerComponent.h"
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
    double computeResumePositionSeconds(AudioEngine& engine, DeckId deck);
    void loadTrackToDeck(DeckId deck, const juce::String& trackId);

    void handleConnectRequested(const ConnectionInfo& info);
    void handleWelcome(const juce::var& welcome);
    void handleRemoteDelta(const StateDelta& delta);
    void handleServerEvent(const juce::var& msg);
    void handleConnectionChange(bool connected, juce::String reason);
    void applyRoleToUI();
    bool canControlLocally() const;
    void pushFullResync();
    void applyDeckSnapshot(DeckId deck, const juce::var& playbackStateVar);
    void applySnapshot(const juce::var& snapshotDecksVar);

    LocalFileRepository repository_;
    TrackListComponent trackList_;

    AudioDeviceHub deviceHub_;
    JuceAudioEngine engineA_;
    JuceAudioEngine engineB_;

    StateManager stateManager_;
    // Declared before engineAdapterA_/engineAdapterB_/mixer_: member init order
    // follows declaration order, mixer_'s constructor reads
    // crossfaderState_.getPosition() and the adapters get attached to it in the
    // constructor body, so this must exist first regardless of initializer list order.
    CrossfaderState crossfaderState_;
    EngineAdapter engineAdapterA_;
    EngineAdapter engineAdapterB_;
    PositionClock positionClock_;
    PositionClock positionClockB_;
    // Declared after positionClock_/positionClockB_: it holds references to both
    // and member init order follows declaration order, so both must exist first.
    DeckPositionClocks deckPositionClocks_;
    std::unique_ptr<SyncTransport> transport_;
    SyncPublisher syncPublisher_;

    ConnectPanel connectPanel_;
    DeckComponent deckA_;
    DeckComponent deckB_;
    MixerComponent mixer_;
    juce::TextButton loadTargetToggle_{"-> A"};

    Role role_ = Role::observer;
    juce::String ownClientId_;
    bool connected_ = false;
    DeckId loadTargetDeck_ = DeckId::A;

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
