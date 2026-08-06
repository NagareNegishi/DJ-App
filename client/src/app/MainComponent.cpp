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
      syncPublisher_(stateManager_, transport_),
      deckA_(stateManager_, DeckId::A, repository_, [this] { return computeResumePositionSeconds(); })
{
    addAndMakeVisible(trackList_);
    trackList_.setTracks(repository_.listAvailableTracks());
    trackList_.onTrackSelected = [this](const TrackMetadata& track)
    {
        StateDelta delta;
        delta.deck = DeckId::A;
        delta.trackId = track.id;
        delta.playing = false;       // a fresh load always starts stopped
        delta.positionSeconds = 0.0; // AudioEngine::load resets to 0; keep the model in agreement
        stateManager_.applyDelta(delta, DeltaSource::local);
    };

    deviceHub_.addSource(engineA_.source());

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

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    auto controls = bounds.removeFromRight(240).reduced(8);
    trackList_.setBounds(bounds);
    deckA_.setBounds(controls);
}

} // namespace djapp
