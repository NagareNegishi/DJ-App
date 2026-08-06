#include "MainComponent.h"

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

} // namespace

MainComponent::MainComponent()
    : repository_(tracksRootDir()), engineAdapterA_(stateManager_, DeckId::A, engineA_, repository_),
      syncPublisher_(stateManager_, transport_)
{
    addAndMakeVisible(trackList_);
    trackList_.setTracks(repository_.listAvailableTracks());

    deviceHub_.addSource(engineA_.source());

    stateManager_.addListener(
        [this](const StateDelta& applied, const PlaybackState& newState, DeltaSource)
        {
            if (applied.deck != DeckId::A)
                return;
            playPauseButton_.setButtonText(newState.playing ? "Pause" : "Play");
        });

    addAndMakeVisible(loadButton_);
    loadButton_.onClick = [this] { loadSelected(); };

    addAndMakeVisible(playPauseButton_);
    playPauseButton_.onClick = [this] { togglePlayPause(); };

    addAndMakeVisible(seekLabel_);
    addAndMakeVisible(seekSlider_);
    seekSlider_.setRange(0.0, 0.0);
    seekSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    seekSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    seekSlider_.onDragEnd = [this]
    {
        StateDelta delta;
        delta.deck = DeckId::A;
        delta.positionSeconds = seekSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(gainLabel_);
    addAndMakeVisible(gainSlider_);
    gainSlider_.setRange(0.0, 2.0);
    gainSlider_.setValue(1.0);
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    gainSlider_.onValueChange = [this]
    {
        StateDelta delta;
        delta.deck = DeckId::A;
        delta.gain = (float)gainSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    addAndMakeVisible(rateLabel_);
    addAndMakeVisible(rateSlider_);
    rateSlider_.setRange(0.5, 2.0);
    rateSlider_.setValue(1.0);
    rateSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    rateSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    rateSlider_.onValueChange = [this]
    {
        StateDelta delta;
        delta.deck = DeckId::A;
        delta.playbackRate = (float)rateSlider_.getValue();
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    setSize(800, 600);
}

void MainComponent::loadSelected()
{
    const auto selected = trackList_.getSelectedTrack();
    if (!selected.has_value())
        return;

    loadedTrackDurationSeconds_ = selected->durationSeconds;
    seekSlider_.setRange(0.0, loadedTrackDurationSeconds_);

    StateDelta delta;
    delta.deck = DeckId::A;
    delta.trackId = selected->id;
    delta.playing = false; // a fresh load always starts stopped, matching the old dev-UI's
                           // explicit setButtonText("Play") after load
    stateManager_.applyDelta(delta, DeltaSource::local);
}

void MainComponent::togglePlayPause()
{
    const bool currentlyPlaying = stateManager_.getState(DeckId::A).playing;

    StateDelta delta;
    delta.deck = DeckId::A;

    if (currentlyPlaying)
    {
        delta.playing = false;
    }
    else
    {
        // EngineAdapter is write-only (state -> engine); nothing feeds the engine's
        // actual playback state back into StateManager until M7's PositionClock. So
        // StateManager's own stored positionSeconds is stale while playing — it only
        // reflects the last explicit seek/load, not where the engine has actually
        // gotten to. Peeking at the engine directly here (dev-UI only, not a general
        // state read) gives the true resume point instead of relying on
        // StateManager's position-injection fallback, which would resume from that
        // stale value.
        double resumePosition = engineA_.getCurrentPosition();

        // If AudioEngine stopped itself at end-of-track (see its isPlaying() contract
        // comment), StateManager's `playing` flag doesn't know that yet, and resuming
        // at that same position would immediately stop again — the exact bug fixed in
        // 73075b9 for the M3 dev UI. Restart from 0 in that case instead.
        if (loadedTrackDurationSeconds_ > 0.0 && !engineA_.isPlaying() &&
            resumePosition >= loadedTrackDurationSeconds_ - 0.05)
        {
            resumePosition = 0.0;
        }

        delta.positionSeconds = resumePosition;
        delta.playing = true;
    }

    stateManager_.applyDelta(delta, DeltaSource::local);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    auto controls = bounds.removeFromRight(240).reduced(8);
    trackList_.setBounds(bounds);

    loadButton_.setBounds(controls.removeFromTop(24));
    controls.removeFromTop(8);
    playPauseButton_.setBounds(controls.removeFromTop(24));
    controls.removeFromTop(16);

    seekLabel_.setBounds(controls.removeFromTop(20));
    seekSlider_.setBounds(controls.removeFromTop(24));
    controls.removeFromTop(16);

    gainLabel_.setBounds(controls.removeFromTop(20));
    gainSlider_.setBounds(controls.removeFromTop(24));
    controls.removeFromTop(16);

    rateLabel_.setBounds(controls.removeFromTop(20));
    rateSlider_.setBounds(controls.removeFromTop(24));
}

} // namespace djapp
