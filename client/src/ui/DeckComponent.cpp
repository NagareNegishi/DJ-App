#include "DeckComponent.h"
#include <cmath>

namespace djapp
{

namespace
{

juce::String formatMinutesSeconds(double totalSeconds)
{
    if (totalSeconds < 0.0 || !std::isfinite(totalSeconds))
        totalSeconds = 0.0;

    const int wholeSeconds = static_cast<int>(totalSeconds);
    const int minutes = wholeSeconds / 60;
    const int seconds = wholeSeconds % 60;
    return juce::String::formatted("%d:%02d", minutes, seconds);
}

} // namespace

DeckComponent::DeckComponent(StateManager& stateManager, DeckId deck, AudioRepository& repository,
                              std::function<double()> resumePositionProvider)
    : stateManager_(stateManager), deck_(deck), repository_(repository),
      resumePositionProvider_(std::move(resumePositionProvider))
{
    addAndMakeVisible(titleLabel_);
    titleLabel_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(playPauseButton_);
    playPauseButton_.onClick = [this] { togglePlayPause(); };

    addAndMakeVisible(positionSlider_);
    positionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    positionSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    positionSlider_.onDragEnd = [this]
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.positionSeconds = positionSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(timeLabel_);
    timeLabel_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(gainSlider_);
    gainSlider_.setRange(0.0, 2.0);
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    gainSlider_.onValueChange = [this]
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.gain = (float)gainSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(rateSlider_);
    rateSlider_.setRange(0.5, 2.0);
    rateSlider_.setDoubleClickReturnValue(true, 1.0); // center-detent at 1.0 (double-click to reset to unity rate)
    rateSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    rateSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    rateSlider_.onValueChange = [this]
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.playbackRate = (float)rateSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(loopInButton_);
    loopInButton_.onClick = [this] { onLoopInClicked(); };

    addAndMakeVisible(loopOutButton_);
    loopOutButton_.onClick = [this] { onLoopOutClicked(); };

    addAndMakeVisible(loopClearButton_);
    loopClearButton_.onClick = [this] { onLoopClearClicked(); };

    listenerToken_ = stateManager_.addListener(
        [this](const StateDelta& applied, const PlaybackState& newState, DeltaSource)
        {
            if (applied.deck != deck_)
                return;
            rebaseAnchorAndRefresh(applied, newState);
        });

    const auto& state = stateManager_.getState(deck_);
    anchorPositionSeconds_ = state.positionSeconds;
    anchorTimestampMs_ = juce::Time::getMillisecondCounterHiRes();
    anchorRate_ = state.playbackRate;
    anchorPlaying_ = state.playing;
    anchorLoop_ = state.loop;
    refreshWidgets(state);

    startTimerHz(30);
}

DeckComponent::~DeckComponent()
{
    stateManager_.removeListener(listenerToken_);
}

void DeckComponent::rebaseAnchorAndRefresh(const StateDelta& applied, const PlaybackState& newState)
{
    if (applied.trackId.has_value() || applied.positionSeconds.has_value())
        anchorPositionSeconds_ = newState.positionSeconds;
    else
        anchorPositionSeconds_ = currentDisplayPositionSeconds(); // reads the anchor fields below, so must run first

    anchorTimestampMs_ = juce::Time::getMillisecondCounterHiRes();
    anchorRate_ = newState.playbackRate;
    anchorPlaying_ = newState.playing;
    anchorLoop_ = newState.loop;

    refreshWidgets(newState);
}

double DeckComponent::currentDisplayPositionSeconds() const
{
    double position = anchorPositionSeconds_;
    if (anchorPlaying_)
    {
        const double elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - anchorTimestampMs_) / 1000.0;
        position += elapsedSeconds * anchorRate_;
    }

    if (anchorLoop_.has_value() && anchorLoop_->outSeconds > anchorLoop_->inSeconds && position >= anchorLoop_->outSeconds)
    {
        const double loopLength = anchorLoop_->outSeconds - anchorLoop_->inSeconds;
        // Same fmod wrap BufferPlaybackSource::getNextAudioBlock uses, so the
        // displayed playhead loops in step with what's audible.
        position = anchorLoop_->inSeconds + std::fmod(position - anchorLoop_->inSeconds, loopLength);
    }

    return position;
}

void DeckComponent::refreshWidgets(const PlaybackState& state)
{
    double duration = 0.0;
    if (state.trackId.isEmpty())
    {
        titleLabel_.setText("No track loaded", juce::dontSendNotification);
    }
    else
    {
        const auto meta = repository_.getTrackMetadata(state.trackId);
        titleLabel_.setText(meta.has_value() ? meta->title : state.trackId, juce::dontSendNotification);
        duration = meta.has_value() ? meta->durationSeconds : 0.0;
    }

    playPauseButton_.setButtonText(state.playing ? "Pause" : "Play");

    positionSlider_.setRange(0.0, duration);
    updatePositionDisplay();

    gainSlider_.setValue(state.gain, juce::dontSendNotification);
    rateSlider_.setValue(state.playbackRate, juce::dontSendNotification);

    const bool hasTrack = !state.trackId.isEmpty();
    playPauseButton_.setEnabled(hasTrack);
    positionSlider_.setEnabled(hasTrack);
    loopInButton_.setEnabled(hasTrack);
    loopClearButton_.setEnabled(hasTrack);
    loopOutButton_.setEnabled(hasTrack && pendingLoopInSeconds_.has_value());
}

void DeckComponent::timerCallback()
{
    updatePositionDisplay();
}

void DeckComponent::updatePositionDisplay()
{
    if (positionSlider_.isMouseButtonDown())
        return; // don't fight an in-progress user drag
    const double currentPosition = currentDisplayPositionSeconds();
    positionSlider_.setValue(currentPosition, juce::dontSendNotification);
    timeLabel_.setText(formatMinutesSeconds(currentPosition) + " / " + formatMinutesSeconds(positionSlider_.getMaximum()),
                        juce::dontSendNotification);
}

void DeckComponent::togglePlayPause()
{
    const bool currentlyPlaying = stateManager_.getState(deck_).playing;

    StateDelta delta;
    delta.deck = deck_;

    if (currentlyPlaying)
    {
        delta.playing = false; // no positionSeconds: rebaseAnchorAndRefresh freezes the playhead where it actually was
    }
    else
    {
        delta.positionSeconds = resumePositionProvider_();
        delta.playing = true;
    }

    stateManager_.applyDelta(delta, DeltaSource::local);
}

void DeckComponent::onLoopInClicked()
{
    pendingLoopInSeconds_ = currentDisplayPositionSeconds();
    loopInButton_.setButtonText("Cancel Loop In");
    loopOutButton_.setEnabled(!stateManager_.getState(deck_).trackId.isEmpty());
}

void DeckComponent::onLoopOutClicked()
{
    if (!pendingLoopInSeconds_.has_value())
    {
        juce::Logger::writeToLog("DeckComponent: Loop Out clicked with no pending Loop In on deck " + toString(deck_));
        return;
    }

    const double outPosition = currentDisplayPositionSeconds();
    if (outPosition > *pendingLoopInSeconds_)
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.loop = LoopPoints{*pendingLoopInSeconds_, outPosition};
        stateManager_.applyDelta(delta, DeltaSource::local);
    }
    else
    {
        juce::Logger::writeToLog("DeckComponent: Loop Out at or before Loop In on deck " + toString(deck_) +
                                 ", ignoring");
    }

    resetPendingLoopIn();
}

void DeckComponent::onLoopClearClicked()
{
    resetPendingLoopIn();

    StateDelta delta;
    delta.deck = deck_;
    delta.loop = std::optional<LoopPoints>{std::nullopt};
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void DeckComponent::resetPendingLoopIn()
{
    pendingLoopInSeconds_.reset();
    loopInButton_.setButtonText("Loop In");
    loopOutButton_.setEnabled(false);
}

void DeckComponent::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);

    playPauseButton_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);

    positionSlider_.setBounds(bounds.removeFromTop(24));
    timeLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(16);

    gainSlider_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);
    rateSlider_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(16);

    auto loopRow = bounds.removeFromTop(24);
    const int loopButtonWidth = loopRow.getWidth() / 3;
    loopInButton_.setBounds(loopRow.removeFromLeft(loopButtonWidth));
    loopOutButton_.setBounds(loopRow.removeFromLeft(loopButtonWidth));
    loopClearButton_.setBounds(loopRow);
}

} // namespace djapp
