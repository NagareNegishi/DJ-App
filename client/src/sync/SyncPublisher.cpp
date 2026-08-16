#include "SyncPublisher.h"

namespace djapp
{

namespace
{

std::size_t deckIndex(DeckId deck)
{
    return deck == DeckId::A ? 0 : 1;
}

// Overwrites each field present in `incoming` onto `pending`, leaving fields
// absent from `incoming` untouched — trailing-edge coalescing: the latest
// value for each field always wins, merged across however many deltas arrived
// before the window opened.
void mergeField(StateDelta& pending, const StateDelta& incoming)
{
    if (incoming.trackId.has_value())
        pending.trackId = incoming.trackId;
    if (incoming.playing.has_value())
        pending.playing = incoming.playing;
    if (incoming.positionSeconds.has_value())
        pending.positionSeconds = incoming.positionSeconds;
    if (incoming.gain.has_value())
        pending.gain = incoming.gain;
    if (incoming.playbackRate.has_value())
        pending.playbackRate = incoming.playbackRate;
    if (incoming.pitchOffsetSemitones.has_value())
        pending.pitchOffsetSemitones = incoming.pitchOffsetSemitones;
    if (incoming.loop.has_value())
        pending.loop = incoming.loop;
}

} // namespace

SyncPublisher::SyncPublisher(StateManager& stateManager, SyncTransport& transport)
    : stateManager_(stateManager), transport_(transport)
{
    listenerToken_ = stateManager_.addListener(
        [this](const StateDelta& applied, const PlaybackState&, DeltaSource source)
        {
            if (source != DeltaSource::local || !connected_ || role_ != Role::controller)
                return;

            handleLocalDelta(applied);
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

void SyncPublisher::handleLocalDelta(const StateDelta& applied)
{
    const auto index = deckIndex(applied.deck);

    if (pending_[index].has_value())
        mergeField(*pending_[index], applied);
    else
        pending_[index] = applied;

    maybeSend(index);
}

void SyncPublisher::maybeSend(std::size_t deckIndex)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (hasSentBefore_[deckIndex] && (now - lastSendMs_[deckIndex]) < kMinIntervalMs)
    {
        // Still inside this deck's throttle window: leave the merged delta
        // pending and make sure a timer is running to flush it once the
        // window opens.
        if (!isTimerRunning())
            startTimerHz(30);
        return;
    }

    transport_.sendDelta(*pending_[deckIndex]);
    pending_[deckIndex].reset();
    lastSendMs_[deckIndex] = now;
    hasSentBefore_[deckIndex] = true;
}

void SyncPublisher::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool anyStillPending = false;

    for (std::size_t i = 0; i < kDeckCount; ++i)
    {
        if (!pending_[i].has_value())
            continue;

        if (!hasSentBefore_[i] || (now - lastSendMs_[i]) >= kMinIntervalMs)
        {
            transport_.sendDelta(*pending_[i]);
            pending_[i].reset();
            lastSendMs_[i] = now;
            hasSentBefore_[i] = true;
        }
        else
        {
            anyStillPending = true;
        }
    }

    if (!anyStillPending)
        stopTimer();
}

void SyncPublisher::flushNow()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    for (std::size_t i = 0; i < kDeckCount; ++i)
    {
        if (!pending_[i].has_value())
            continue;

        transport_.sendDelta(*pending_[i]);
        pending_[i].reset();
        lastSendMs_[i] = now;
        hasSentBefore_[i] = true;
    }

    stopTimer();
}

} // namespace djapp
