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

    setSize(800, 600);
}

void MainComponent::resized()
{
    trackList_.setBounds(getLocalBounds());
}

} // namespace djapp
