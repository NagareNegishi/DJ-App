#pragma once

// app/ — composition root. MainComponent owns repository/engine/state/sync
// wiring as later milestones land; at M2 it owns the AudioRepository and the
// TrackListComponent that lists what it finds.

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
    LocalFileRepository repository_;
    TrackListComponent trackList_;
};

} // namespace djapp
