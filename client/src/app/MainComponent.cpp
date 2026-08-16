#include "MainComponent.h"

#include "model/Ranges.h"
#include "model/Serialization.h"
#include "sync/WebSocketTransport.h"

#include <algorithm>

namespace djapp
{

namespace
{

// CLIENT_SOURCE_DIR is set by CMake to the client/ directory's absolute path;
// works unmodified for a plain container build/run, this repo's actual dev
// workflow. Revisit if the app is ever packaged for distribution.
juce::File tracksRootDir()
{
    return juce::File(CLIENT_SOURCE_DIR).getChildFile("assets/tracks");
}

Role roleFromWireString(const juce::String& s)
{
    return s == "controller" ? Role::controller : Role::observer;
}

} // namespace

MainComponent::MainComponent()
    : repository_(tracksRootDir()), engineAdapterA_(stateManager_, DeckId::A, engineA_, repository_),
      positionClock_(stateManager_, engineA_, DeckId::A), transport_(std::make_unique<WebSocketTransport>()),
      syncPublisher_(stateManager_, *transport_),
      deckA_(stateManager_, DeckId::A, repository_, [this] { return computeResumePositionSeconds(); })
{
    addAndMakeVisible(trackList_);
    trackList_.setTracks(repository_.listAvailableTracks());
    trackList_.onTrackSelected = [this](const TrackMetadata& track)
    {
        // Second line of defense against an observer double-clicking a track:
        // setEnabled(controlsEnabled) below is the visible one; this guard is
        // the one that actually matters (06-security.md: client-side disabling
        // is UX, not security).
        if (role_ != Role::controller)
            return;

        StateDelta delta;
        delta.deck = DeckId::A;
        delta.trackId = track.id;
        delta.playing = false;       // a fresh load always starts stopped
        delta.positionSeconds = 0.0; // AudioEngine::load resets to 0; keep the model in agreement
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    deviceHub_.addSource(engineA_.source());

    addAndMakeVisible(connectPanel_);
    connectPanel_.onConnectRequested = [this](const ConnectionInfo& info) { handleConnectRequested(info); };
    connectPanel_.onDisconnectRequested = [this] { transport_->disconnect(); };
    connectPanel_.onClaimRequested = [this] { transport_->sendClaimControl(); };
    connectPanel_.onReleaseRequested = [this] { transport_->sendReleaseControl(); };

    addAndMakeVisible(deckA_);

    setSize(800, 600);
}

double MainComponent::computeResumePositionSeconds()
{
    double resumePosition = engineA_.getCurrentPosition();
    const auto meta = repository_.getTrackMetadata(stateManager_.getState(DeckId::A).trackId);
    const double duration = meta.has_value() ? meta->durationSeconds : 0.0;
    // AudioEngine can stop itself at end-of-track without telling StateManager, so
    // resuming at the same stale position would immediately stop again — reset to 0 instead.
    if (duration > 0.0 && !engineA_.isPlaying() && resumePosition >= duration - 0.05)
        resumePosition = 0.0;
    return resumePosition;
}

void MainComponent::handleConnectRequested(const ConnectionInfo& info)
{
    SyncTransport::Callbacks callbacks;
    callbacks.onWelcome = [this](const juce::var& welcome) { handleWelcome(welcome); };
    callbacks.onRemoteDelta = [this](const StateDelta& delta) { handleRemoteDelta(delta); };
    callbacks.onServerEvent = [this](const juce::var& msg) { handleServerEvent(msg); };
    callbacks.onConnectionChange = [this](bool connected, juce::String reason)
    { handleConnectionChange(connected, std::move(reason)); };
    transport_->connect(info, std::move(callbacks));
}

void MainComponent::handleWelcome(const juce::var& welcome)
{
    auto* obj = welcome.getDynamicObject();
    if (obj == nullptr)
        return;

    ownClientId_ = obj->getProperty("clientId").toString();
    role_ = roleFromWireString(obj->getProperty("role").toString());

    if (auto* snapshotObj = obj->getProperty("snapshot").getDynamicObject())
        applySnapshot(snapshotObj->getProperty("decks"));

    peers_.clear();
    const auto peersVar = obj->getProperty("peers");
    if (peersVar.isArray())
    {
        for (auto& entry : *peersVar.getArray())
        {
            auto* peerObj = entry.getDynamicObject();
            if (peerObj == nullptr || !peerObj->hasProperty("clientId") || !peerObj->hasProperty("name") ||
                !peerObj->hasProperty("role"))
                continue; // network data, treat defensively rather than crash

            ConnectPanel::PeerInfo peer;
            peer.clientId = peerObj->getProperty("clientId").toString();
            peer.name = peerObj->getProperty("name").toString();
            peer.role = roleFromWireString(peerObj->getProperty("role").toString());
            peers_.push_back(std::move(peer));
        }
    }
    connectPanel_.setPeers(peers_);

    applyRoleToUI();
}

void MainComponent::handleRemoteDelta(const StateDelta& delta)
{
    stateManager_.applyDelta(delta, DeltaSource::remote);
}

void MainComponent::handleServerEvent(const juce::var& msg)
{
    const auto type = messageType(msg);
    auto* obj = msg.getDynamicObject();
    if (obj == nullptr)
        return;

    if (type == "snapshot")
    {
        applySnapshot(obj->getProperty("decks"));
        return;
    }

    if (type == "roleChanged")
    {
        const auto clientId = obj->getProperty("clientId").toString();
        const auto role = roleFromWireString(obj->getProperty("role").toString());

        if (clientId == ownClientId_)
        {
            role_ = role;
            applyRoleToUI();
        }
        else
        {
            for (auto& peer : peers_)
                if (peer.clientId == clientId)
                    peer.role = role;
            connectPanel_.setPeers(peers_);
        }
        return;
    }

    if (type == "peerJoined")
    {
        if (!obj->hasProperty("clientId") || !obj->hasProperty("name") || !obj->hasProperty("role"))
            return;

        ConnectPanel::PeerInfo peer;
        peer.clientId = obj->getProperty("clientId").toString();
        peer.name = obj->getProperty("name").toString();
        peer.role = roleFromWireString(obj->getProperty("role").toString());
        peers_.push_back(std::move(peer));
        connectPanel_.setPeers(peers_);
        return;
    }

    if (type == "peerLeft")
    {
        const auto clientId = obj->getProperty("clientId").toString();
        peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                    [&](const ConnectPanel::PeerInfo& peer) { return peer.clientId == clientId; }),
                    peers_.end());
        connectPanel_.setPeers(peers_);
        return;
    }

    if (type == "error")
    {
        juce::Logger::writeToLog("MainComponent: server error " + obj->getProperty("code").toString() + ": " +
                                 obj->getProperty("message").toString());
        return;
    }
}

void MainComponent::handleConnectionChange(bool connected, juce::String reason)
{
    connected_ = connected;
    syncPublisher_.setConnected(connected);
    connectPanel_.setConnectionStatus(connected, reason);

    if (!connected)
    {
        role_ = Role::observer;
        ownClientId_.clear();
        peers_.clear();
        connectPanel_.setPeers({});
        applyRoleToUI();
    }
}

void MainComponent::applyRoleToUI()
{
    connectPanel_.setRole(role_);
    positionClock_.setRole(role_);
    syncPublisher_.setRole(role_); // the only other role-gated consumer; without this the
                                   // controller would never actually forward its own deltas

    // Deck/track-list enablement is not role alone: solo local playback (never
    // connected) must keep working exactly as it did at M3-M6, and role only
    // gates once actually connected.
    const bool controlsEnabled = !connected_ || role_ == Role::controller;
    deckA_.setControlsEnabled(controlsEnabled);
    trackList_.setEnabled(controlsEnabled);
}

void MainComponent::applyDeckSnapshot(DeckId deck, const juce::var& playbackStateVar)
{
    auto parsed = fromVar<PlaybackState>(playbackStateVar);
    if (!parsed)
    {
        juce::Logger::writeToLog("MainComponent: malformed snapshot for deck " + toString(deck) + ": " +
                                 parsed.error);
        return;
    }

    auto state = *parsed;
    ranges::clamp(state);

    StateDelta delta;
    delta.deck = deck;
    delta.trackId = state.trackId;
    delta.playing = state.playing;
    delta.positionSeconds = state.positionSeconds;
    delta.gain = state.gain;
    delta.playbackRate = state.playbackRate;
    delta.pitchOffsetSemitones = state.pitchOffsetSemitones;
    delta.loop = state.loop; // outer optional set explicit ("field present"); inner is null-vs-value

    stateManager_.applyDelta(delta, DeltaSource::remote);
}

void MainComponent::applySnapshot(const juce::var& snapshotDecksVar)
{
    auto* decksObj = snapshotDecksVar.getDynamicObject();
    if (decksObj == nullptr)
        return;

    if (decksObj->hasProperty("A"))
        applyDeckSnapshot(DeckId::A, decksObj->getProperty("A"));
    if (decksObj->hasProperty("B"))
        applyDeckSnapshot(DeckId::B, decksObj->getProperty("B"));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    connectPanel_.setBounds(bounds.removeFromTop(120));
    auto controls = bounds.removeFromRight(240).reduced(8);
    trackList_.setBounds(bounds);
    deckA_.setBounds(controls);
}

} // namespace djapp
