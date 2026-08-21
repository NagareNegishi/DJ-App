#include "MainComponent.h"

#include "model/ControlGating.h"
#include "model/FullResyncDelta.h"
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

// Whether any known peer currently holds controller; used to distinguish
// "no one is in control" (deck controls should be open to an observer) from
// "someone else is in control" (deck controls stay locked).
bool anyPeerControls(const std::vector<ConnectPanel::PeerInfo>& peers)
{
    return std::any_of(peers.begin(), peers.end(),
                       [](const ConnectPanel::PeerInfo& peer) { return peer.role == Role::controller; });
}

// 02-protocol.md's Room and control model: max 8 clients.
constexpr std::size_t kMaxPeers = 8;

// Adds `peer` to `peers`, or updates an existing entry with the same clientId in
// place. Rejects (silently skips) a peer whose name fails the same shape check
// outbound hello names get, and refuses to grow `peers` past the protocol's max
// room size — a malicious server forging peerJoined frames must not be able to
// inject forged multiline names or grow this list without bound.
void addOrUpdatePeer(std::vector<ConnectPanel::PeerInfo>& peers, ConnectPanel::PeerInfo peer, bool& loggedCapExceeded)
{
    if (!isValidPeerName(peer.name))
        return;

    for (auto& existing : peers)
    {
        if (existing.clientId == peer.clientId)
        {
            existing = std::move(peer);
            return;
        }
    }

    if (peers.size() >= kMaxPeers)
    {
        if (!loggedCapExceeded)
        {
            loggedCapExceeded = true;
            juce::Logger::writeToLog("MainComponent: dropping peer, room already at max size");
        }
        return;
    }

    peers.push_back(std::move(peer));
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
        // Second line of defense against an observer double-clicking a track
        // (06-security.md: client-side disabling is UX, not security).
        if (!canControlLocally())
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
    connectPanel_.onDisconnectRequested = [this]
    {
        // WebSocketTransport::disconnect() flips its alive flag before stopping the
        // socket, so a self-initiated disconnect never fires onConnectionChange the
        // way a server-side drop would — drive the same transition here ourselves.
        transport_->disconnect();
        handleConnectionChange(false, "disconnected");
    };
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
            addOrUpdatePeer(peers_, std::move(peer), loggedPeerCapExceeded_);
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
            const bool becameController = role == Role::controller && role_ != Role::controller;
            role_ = role;
            applyRoleToUI();
            // The room's canonical state may not match what this client was already
            // playing (e.g. solo, unsynced, before claiming) - catch peers up now
            // rather than waiting on whichever field the next UI action happens to touch.
            if (becameController)
                pushFullResync();
        }
        else
        {
            for (auto& peer : peers_)
                if (peer.clientId == clientId)
                    peer.role = role;
            connectPanel_.setPeers(peers_);
            applyRoleToUI();
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
        addOrUpdatePeer(peers_, std::move(peer), loggedPeerCapExceeded_);
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
        applyRoleToUI();
        return;
    }

    if (type == "error")
    {
        // "code" is drawn from a small fixed whitelist (02-protocol.md) and is safe to
        // log verbatim; "message" is server-controlled free text and must never be
        // logged (log-line forging / disk-fill via an attacker-chosen ~4KB frame).
        if (!loggedServerError_)
        {
            loggedServerError_ = true;
            juce::Logger::writeToLog(
                "MainComponent: server sent an error frame (code: " + obj->getProperty("code").toString() + ")");
        }
        return;
    }
}

void MainComponent::handleConnectionChange(bool connected, juce::String reason)
{
    connected_ = connected;
    syncPublisher_.setConnected(connected);
    connectPanel_.setConnectionStatus(connected, reason);

    if (connected)
    {
        // Fresh connection: the once-per-connection log guards below start over.
        loggedServerError_ = false;
        loggedPeerCapExceeded_ = false;
    }
    else
    {
        role_ = Role::observer;
        ownClientId_.clear();
        peers_.clear();
        connectPanel_.setPeers({});
        applyRoleToUI();
    }
}

bool MainComponent::canControlLocally() const
{
    return controlsEnabledLocally(connected_, role_ == Role::controller, anyPeerControls(peers_));
}

void MainComponent::pushFullResync()
{
    // Sent straight through transport_, not via stateManager_.applyDelta: the latter
    // would also notify this client's own EngineAdapter, which reloads the track on
    // any trackId-bearing delta and resets position to 0 - an audible restart of
    // playback that hasn't actually changed, right as this client claims control.
    transport_->sendDelta(fullResyncDelta(DeckId::A, stateManager_.getState(DeckId::A)));
}

void MainComponent::applyRoleToUI()
{
    connectPanel_.setRole(role_);
    positionClock_.setRole(role_);
    syncPublisher_.setRole(role_); // the only other role-gated consumer; without this the
                                   // controller would never actually forward its own deltas

    const bool controlsEnabled = canControlLocally();
    deckA_.setControlsEnabled(controlsEnabled);
    trackList_.setEnabled(controlsEnabled);
}

void MainComponent::applyDeckSnapshot(DeckId deck, const juce::var& playbackStateVar)
{
    auto parsed = fromVar<PlaybackState>(playbackStateVar);
    if (!parsed)
    {
        juce::Logger::writeToLog("MainComponent: malformed snapshot for deck " + toString(deck) + ": " + parsed.error);
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
