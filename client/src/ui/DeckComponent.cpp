#include "DeckComponent.h"
#include "model/BeatSync.h"
#include "model/ControlGating.h"
#include "model/LoopWrap.h"
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

// M9: this app's first icon-based control (everything else is a
// juce::TextButton), so there's no existing glyph-drawing helper to reuse.
// A standard "repeat" symbol: an open circular arc with a small triangular
// arrowhead at its leading end. Drawn in a 24x24 box.
juce::Path makeRepeatGlyph()
{
    constexpr float centreX = 12.0f, centreY = 12.0f, radius = 8.0f;
    constexpr float startRadians = juce::MathConstants<float>::pi * 0.15f;
    constexpr float endRadians = juce::MathConstants<float>::pi * 1.75f;

    juce::Path path;
    path.addCentredArc(centreX, centreY, radius, radius, 0.0f, startRadians, endRadians, true);

    const float tipX = centreX + radius * std::sin(endRadians);
    const float tipY = centreY - radius * std::cos(endRadians);
    const float tangentX = std::cos(endRadians);
    const float tangentY = std::sin(endRadians);
    constexpr float headLength = 6.0f, headWidth = 5.0f;

    juce::Path arrowHead;
    arrowHead.addTriangle(tipX - tangentX * headLength - tangentY * headWidth,
                          tipY - tangentY * headLength + tangentX * headWidth,
                          tipX + tangentX * headLength - tangentY * headWidth,
                          tipY + tangentY * headLength + tangentX * headWidth, tipX, tipY);
    path.addPath(arrowHead);
    return path;
}

} // namespace

DeckComponent::DeckComponent(StateManager& stateManager, DeckId deck, AudioRepository& repository,
                             std::function<double()> resumePositionProvider,
                             std::function<DeckSyncInfo()> thisDeckSyncInfoProvider,
                             std::function<DeckSyncInfo()> otherDeckSyncInfoProvider)
    : stateManager_(stateManager), deck_(deck), repository_(repository),
      resumePositionProvider_(std::move(resumePositionProvider)),
      thisDeckSyncInfoProvider_(std::move(thisDeckSyncInfoProvider)),
      otherDeckSyncInfoProvider_(std::move(otherDeckSyncInfoProvider))
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

    addAndMakeVisible(pitchSlider_);
    pitchSlider_.setRange(-12.0, 12.0);
    pitchSlider_.setDoubleClickReturnValue(true, 0.0); // center-detent at 0 semitones (natural neutral value)
    pitchSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    pitchSlider_.onValueChange = [this]
    {
        StateDelta delta;
        delta.deck = deck_;
        delta.pitchOffsetSemitones = (float)pitchSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(syncButton_);
    syncButton_.onClick = [this] { onSyncClicked(); };

    addAndMakeVisible(loopInButton_);
    loopInButton_.onClick = [this] { onLoopInClicked(); };

    addAndMakeVisible(loopOutButton_);
    loopOutButton_.onClick = [this] { onLoopOutClicked(); };

    addAndMakeVisible(loopClearButton_);
    loopClearButton_.onClick = [this] { onLoopClearClicked(); };

    addAndMakeVisible(repeatButton_);
    {
        juce::DrawablePath offImage;
        offImage.setPath(makeRepeatGlyph());
        offImage.setFill(juce::Colours::transparentBlack);
        offImage.setStrokeFill(juce::Colours::lightgrey);
        offImage.setStrokeThickness(2.0f);

        juce::DrawablePath onImage;
        onImage.setPath(makeRepeatGlyph());
        onImage.setFill(juce::Colours::limegreen);
        onImage.setStrokeFill(juce::Colours::limegreen);
        onImage.setStrokeThickness(2.0f);

        repeatButton_.setImages(&offImage, nullptr, nullptr, nullptr, &onImage);
    }
    repeatButton_.onClick = [this] { onRepeatToggled(); };

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
    anchorRepeat_ = state.repeat;
    refreshWidgets(state);

    startTimerHz(30);
}

DeckComponent::~DeckComponent()
{
    stateManager_.removeListener(listenerToken_);
}

void DeckComponent::setControlsEnabled(bool enabled)
{
    rolePermitsControl_ = enabled;
    refreshWidgets(stateManager_.getState(deck_));
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
    anchorRepeat_ = newState.repeat;

    // A track change invalidates any in-flight loop-arm gesture: the pending
    // in-point and stashed loop were captured against the previous track's
    // timeline, and applying either to the new track would be wrong.
    if (applied.trackId.has_value())
        resetPendingLoopIn();

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

    if (anchorLoop_.has_value())
    {
        const double duration = positionSlider_.getMaximum();
        const double clampedIn =
            duration > 0.0 && anchorLoop_->inSeconds > duration ? duration : anchorLoop_->inSeconds;
        const double clampedOut =
            duration > 0.0 && anchorLoop_->outSeconds > duration ? duration : anchorLoop_->outSeconds;
        position = wrapPositionWithinRange(position, clampedIn, clampedOut);
    }
    else if (anchorRepeat_)
        position = wrapPositionWithinRange(position, 0.0, positionSlider_.getMaximum());

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
        titleLabel_.setText(meta.has_value() ? meta->title : "missing track: " + state.trackId,
                            juce::dontSendNotification);
        duration = meta.has_value() ? meta->durationSeconds : 0.0;
    }

    playPauseButton_.setButtonText(state.playing ? "Pause" : "Play");

    positionSlider_.setRange(0.0, duration);
    updatePositionDisplay();

    gainSlider_.setValue(state.gain, juce::dontSendNotification);
    rateSlider_.setValue(state.playbackRate, juce::dontSendNotification);
    pitchSlider_.setValue(state.pitchOffsetSemitones, juce::dontSendNotification);

    // setToggleState() no-ops while disabled, so force-enable around the call.
    const bool hasTrack = !state.trackId.isEmpty();
    const bool controlEnabled = deckControlEnabled(hasTrack, rolePermitsControl_);
    repeatButton_.setEnabled(true);
    repeatButton_.setToggleState(state.repeat, juce::dontSendNotification);
    repeatButton_.setEnabled(controlEnabled);

    playPauseButton_.setEnabled(controlEnabled);
    positionSlider_.setEnabled(controlEnabled);
    loopInButton_.setEnabled(controlEnabled);
    loopClearButton_.setEnabled(controlEnabled);
    loopOutButton_.setEnabled(controlEnabled && pendingLoopInSeconds_.has_value());
    gainSlider_.setEnabled(controlEnabled);
    rateSlider_.setEnabled(controlEnabled);
    pitchSlider_.setEnabled(controlEnabled);
    syncButton_.setEnabled(controlEnabled);
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
    timeLabel_.setText(formatMinutesSeconds(currentPosition) + " / " +
                           formatMinutesSeconds(positionSlider_.getMaximum()),
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
    if (pendingLoopInSeconds_.has_value())
    {
        cancelPendingLoopIn();
        return;
    }

    // Capture before touching state below: clearing an active loop doesn't
    // move the playhead (currentDisplayPositionSeconds() returns the same
    // wrapped value immediately before and after), so capturing first gives
    // an identical result without depending on the clear's re-entrant
    // notification having already run.
    pendingLoopInSeconds_ = currentDisplayPositionSeconds();

    stashedLoopOnArm_ = stateManager_.getState(deck_).loop;
    if (stashedLoopOnArm_.has_value())
    {
        StateDelta clearDelta;
        clearDelta.deck = deck_;
        clearDelta.loop = std::optional<LoopPoints>{std::nullopt};
        stateManager_.applyDelta(clearDelta, DeltaSource::local);
    }

    loopInButton_.setButtonText("Cancel Loop In");
    loopOutButton_.setEnabled(!stateManager_.getState(deck_).trackId.isEmpty() && rolePermitsControl_);
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
        resetPendingLoopIn();
    }
    else
    {
        juce::Logger::writeToLog("DeckComponent: Loop Out at or before Loop In on deck " + toString(deck_) +
                                 ", ignoring");
        cancelPendingLoopIn(); // restores the stash (if any) and ends the gesture
    }
}

void DeckComponent::onLoopClearClicked()
{
    resetPendingLoopIn();

    StateDelta delta;
    delta.deck = deck_;
    delta.loop = std::optional<LoopPoints>{std::nullopt};
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void DeckComponent::onRepeatToggled()
{
    StateDelta delta;
    delta.deck = deck_;
    delta.repeat = !stateManager_.getState(deck_).repeat;
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void DeckComponent::onSyncClicked()
{
    const DeckSyncInfo thisInfo = thisDeckSyncInfoProvider_();
    const DeckSyncInfo otherInfo = otherDeckSyncInfoProvider_();

    double thisDurationSeconds = 0.0;
    const auto meta = repository_.getTrackMetadata(stateManager_.getState(deck_).trackId);
    if (meta.has_value())
        thisDurationSeconds = meta->durationSeconds;

    const auto result = computeBeatSync(thisInfo.beatGrid, thisInfo.positionSeconds, otherInfo.beatGrid,
                                        otherInfo.positionSeconds, otherInfo.playbackRate, thisDurationSeconds);
    if (!result.has_value())
    {
        juce::Logger::writeToLog("DeckComponent: sync clicked on deck " + toString(deck_) +
                                 " but beat detection hasn't succeeded on one side, ignoring");
        return;
    }

    StateDelta delta;
    delta.deck = deck_;
    delta.playbackRate = result->playbackRate;
    delta.positionSeconds = result->positionSeconds;
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void DeckComponent::resetPendingLoopIn()
{
    pendingLoopInSeconds_.reset();
    stashedLoopOnArm_.reset();
    loopInButton_.setButtonText("Loop In");
    loopOutButton_.setEnabled(rolePermitsControl_ && false);
}

void DeckComponent::cancelPendingLoopIn()
{
    if (stashedLoopOnArm_.has_value())
    {
        StateDelta restoreDelta;
        restoreDelta.deck = deck_;
        restoreDelta.loop = stashedLoopOnArm_;
        stateManager_.applyDelta(restoreDelta, DeltaSource::local);
    }
    resetPendingLoopIn(); // also clears stashedLoopOnArm_
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
    bounds.removeFromTop(8);

    auto pitchRow = bounds.removeFromTop(24);
    syncButton_.setBounds(pitchRow.removeFromRight(60));
    pitchRow.removeFromRight(4);
    pitchSlider_.setBounds(pitchRow);
    bounds.removeFromTop(16);

    auto loopRow = bounds.removeFromTop(24);
    repeatButton_.setBounds(loopRow.removeFromRight(24));
    loopRow.removeFromRight(4);
    const int loopButtonWidth = loopRow.getWidth() / 3;
    loopInButton_.setBounds(loopRow.removeFromLeft(loopButtonWidth));
    loopOutButton_.setBounds(loopRow.removeFromLeft(loopButtonWidth));
    loopClearButton_.setBounds(loopRow);
}

} // namespace djapp
