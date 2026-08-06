#pragma once

// app/ — composition root. MainComponent owns repository/engine/state/sync
// wiring, growing as later milestones land. At M4 it owns the AudioRepository,
// the TrackListComponent that lists what it finds, one deck's worth of engine
// wiring, a dev UI that drives StateManager (never the engine directly), and
// the sync stack (NullTransport + SyncPublisher) -- wired but inert until M7
// gives it a real transport and something to call setConnected/setRole.

#include "engine/AudioDeviceHub.h"
#include "engine/EngineAdapter.h"
#include "engine/JuceAudioEngine.h"
#include "repository/LocalFileRepository.h"
#include "state/StateManager.h"
#include "sync/NullTransport.h"
#include "sync/SyncPublisher.h"
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

    StateManager stateManager_;
    EngineAdapter engineAdapterA_;
    NullTransport transport_;
    SyncPublisher syncPublisher_;

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
