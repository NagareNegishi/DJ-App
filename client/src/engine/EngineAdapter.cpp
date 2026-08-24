#include "EngineAdapter.h"
#include "model/CrossfaderCurve.h"
#include <juce_events/juce_events.h>

namespace djapp
{

EngineAdapter::EngineAdapter(StateManager& stateManager, DeckId deck, AudioEngine& engine, AudioRepository& repository)
    : stateManager_(stateManager), deck_(deck), engine_(engine), repository_(repository)
{
    JUCE_ASSERT_MESSAGE_THREAD

    listenerToken_ = stateManager_.addListener([this](const StateDelta& applied, const PlaybackState& newState,
                                                      DeltaSource source) { handleDelta(applied, newState, source); });
}

EngineAdapter::~EngineAdapter()
{
    stateManager_.removeListener(listenerToken_);
    if (crossfader_ != nullptr)
        crossfader_->removeListener(crossfaderListenerToken_);
}

void EngineAdapter::attachCrossfader(CrossfaderState& crossfader)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (crossfader_ != nullptr)
        crossfader_->removeListener(crossfaderListenerToken_);

    crossfader_ = &crossfader;
    crossfaderListenerToken_ =
        crossfader_->addListener([this](float) { pushEffectiveGain(stateManager_.getState(deck_).gain); });

    pushEffectiveGain(stateManager_.getState(deck_).gain);
}

void EngineAdapter::attachBeatDetector(BeatDetector& detector)
{
    JUCE_ASSERT_MESSAGE_THREAD

    beatDetector_ = &detector;
}

void EngineAdapter::pushEffectiveGain(float gain)
{
    float multiplier = 1.0f;
    if (crossfader_ != nullptr)
    {
        const auto gains = equalPowerCrossfade(crossfader_->getPosition());
        multiplier = deck_ == DeckId::A ? gains.gainA : gains.gainB;
    }

    engine_.setGain(gain * multiplier);
}

void EngineAdapter::handleDelta(const StateDelta& applied, const PlaybackState& /*newState*/, DeltaSource /*source*/)
{
    if (applied.deck != deck_)
        return;

    if (applied.trackId.has_value())
    {
        if (applied.trackId->isEmpty())
        {
            juce::Logger::writeToLog("EngineAdapter: track cleared on deck " + toString(deck_) +
                                     "; unload not supported, ignoring");
            engine_.load(nullptr);
            cachedBeatGrid_ = BeatGrid{};
            lastAnalyzedTrackId_.clear();
        }
        else
        {
            const auto buffer = repository_.getAudioBuffer(*applied.trackId);
            if (buffer == nullptr)
            {
                juce::Logger::writeToLog("EngineAdapter: failed to load audio for track \"" + *applied.trackId + "\"");
                // Don't leave the previously loaded track audible at the new position -
                // protocol requires clients missing this track to go silent.
                engine_.load(nullptr);
                cachedBeatGrid_ = BeatGrid{};
                lastAnalyzedTrackId_.clear();
            }
            else
            {
                engine_.load(buffer);
                if (beatDetector_ != nullptr && *applied.trackId != lastAnalyzedTrackId_)
                {
                    cachedBeatGrid_ = beatDetector_->analyze(*buffer);
                    lastAnalyzedTrackId_ = *applied.trackId;
                }
            }
        }
    }

    if (applied.positionSeconds.has_value())
        engine_.seek(*applied.positionSeconds);

    if (applied.gain.has_value())
        pushEffectiveGain(*applied.gain);

    if (applied.playbackRate.has_value())
        engine_.setPlaybackRate(*applied.playbackRate);

    if (applied.pitchOffsetSemitones.has_value())
        engine_.setPitchOffsetSemitones(*applied.pitchOffsetSemitones);

    if (applied.loop.has_value())
        engine_.setLoop(*applied.loop);

    if (applied.repeat.has_value())
        engine_.setRepeat(*applied.repeat);

    if (applied.playing.has_value())
    {
        *applied.playing ? engine_.play() : engine_.pause();
        *applied.playing ? startTimerHz(10) : stopTimer();
    }
}

void EngineAdapter::timerCallback()
{
    checkForSelfStop();
}

void EngineAdapter::checkForSelfStop()
{
    if (stateManager_.getState(deck_).playing && !engine_.isPlaying())
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.playing = false; // no positionSeconds: mirrors DeckComponent's own
                               // manual-pause delta, freezing the displayed
                               // position rather than resetting it
        stateManager_.applyDelta(delta, DeltaSource::local);
    }
}

} // namespace djapp
