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

MainComponent::MainComponent() : repository_(tracksRootDir())
{
    addAndMakeVisible(trackList_);
    trackList_.setTracks(repository_.listAvailableTracks());

    deviceHub_.addSource(engineA_.source());

    // M4 replaces: this whole block wires dev controls straight to engineA_;
    // delete it once StateManager/StateDelta routing exists.
    addAndMakeVisible(loadButton_);
    loadButton_.onClick = [this] { loadSelected(); };

    addAndMakeVisible(playPauseButton_);
    playPauseButton_.onClick = [this] { togglePlayPause(); };

    addAndMakeVisible(seekLabel_);
    addAndMakeVisible(seekSlider_);
    seekSlider_.setRange(0.0, 0.0);
    seekSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    seekSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    seekSlider_.onDragEnd = [this] { engineA_.seek(seekSlider_.getValue()); };

    addAndMakeVisible(gainLabel_);
    addAndMakeVisible(gainSlider_);
    gainSlider_.setRange(0.0, 2.0);
    gainSlider_.setValue(1.0);
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    gainSlider_.onValueChange = [this] { engineA_.setGain((float)gainSlider_.getValue()); };

    addAndMakeVisible(rateLabel_);
    addAndMakeVisible(rateSlider_);
    rateSlider_.setRange(0.5, 2.0);
    rateSlider_.setValue(1.0);
    rateSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    rateSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    rateSlider_.onValueChange = [this] { engineA_.setPlaybackRate((float)rateSlider_.getValue()); };

    setSize(800, 600);
}

void MainComponent::loadSelected()
{
    const auto selected = trackList_.getSelectedTrack();
    if (!selected.has_value())
        return;

    const auto buffer = repository_.getAudioBuffer(selected->id);
    if (buffer == nullptr)
    {
        juce::Logger::writeToLog("MainComponent: failed to load audio for track \"" + selected->id + "\"");
        return;
    }

    engineA_.load(buffer);
    loadedTrackDurationSeconds_ = selected->durationSeconds;
    seekSlider_.setRange(0.0, loadedTrackDurationSeconds_);
    playPauseButton_.setButtonText("Play");
}

void MainComponent::togglePlayPause()
{
    if (engineA_.isPlaying())
    {
        engineA_.pause();
        playPauseButton_.setButtonText("Play");
    }
    else
    {
        // isPlaying() can go false on its own at end-of-track, holding position at the
        // end (AudioEngine contract) — restart from 0 rather than resuming there and
        // immediately re-stopping.
        if (loadedTrackDurationSeconds_ > 0.0 &&
            engineA_.getCurrentPosition() >= loadedTrackDurationSeconds_ - 0.05)
            engineA_.seek(0.0);

        engineA_.play();
        playPauseButton_.setButtonText("Pause");
    }
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
