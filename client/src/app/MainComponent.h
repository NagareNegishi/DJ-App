#pragma once

// app/ — composition root. MainComponent will own repository/engine/state/sync
// wiring as later milestones land; for now it is an empty top-level view.

#include <juce_gui_basics/juce_gui_basics.h>

namespace djapp
{

class MainComponent : public juce::Component
{
public:
    MainComponent();
};

} // namespace djapp
