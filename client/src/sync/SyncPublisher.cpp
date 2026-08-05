#include "SyncPublisher.h"

namespace djapp
{

SyncPublisher::SyncPublisher(StateManager& stateManager, SyncTransport& transport)
    : stateManager_(stateManager), transport_(transport)
{
    listenerToken_ = stateManager_.addListener(
        [this](const StateDelta& applied, const PlaybackState&, DeltaSource source)
        {
            if (source != DeltaSource::local || !connected_ || role_ != Role::controller)
                return;

            // Throttling to <=30/s per control (trailing-edge coalescing) lands
            // with WebSocketTransport at M7 — NullTransport discards everything.
            transport_.sendDelta(applied);
        });
}

SyncPublisher::~SyncPublisher()
{
    stateManager_.removeListener(listenerToken_);
}

void SyncPublisher::setConnected(bool connected)
{
    connected_ = connected;
}

void SyncPublisher::setRole(Role role)
{
    role_ = role;
}

} // namespace djapp
