#pragma once

// app/ — composition root. MainComponent owns repository/engine/state/sync
// wiring as later milestones land; at M3 it owns the AudioRepository, the
// TrackListComponent that lists what it finds, one deck's worth of engine
// wiring, and a temporary dev UI that drives the engine directly (see the
// // M4 replaces block in MainComponent.cpp — StateManager takes over that
// wiring at M4).

#include "engine/AudioDeviceHub.h"
#include "engine/JuceAudioEngine.h"
#include "repository/LocalFileRepository.h"
#include "ui/TrackListComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace djapp
{

class MainComponent : public juce::Component
{
  public:
    MainComponent();

    void resized() override;

  private:
    void loadSelected();
    void togglePlayPause();

    LocalFileRepository repository_;
    TrackListComponent trackList_;

    AudioDeviceHub deviceHub_;
    JuceAudioEngine engineA_;

    // M4 replaces: dev-only controls calling engineA_ directly; StateManager
    // routes these through deltas instead once it lands.
    juce::TextButton loadButton_{"Load Selected"};
    juce::TextButton playPauseButton_{"Play"};
    juce::Label seekLabel_{{}, "Seek"};
    juce::Slider seekSlider_;
    juce::Label gainLabel_{{}, "Gain"};
    juce::Slider gainSlider_;
    juce::Label rateLabel_{{}, "Rate"};
    juce::Slider rateSlider_;
    double loadedTrackDurationSeconds_ = 0;
};

} // namespace djapp
