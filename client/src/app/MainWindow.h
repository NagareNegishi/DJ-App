#pragma once

// app/ — composition root. MainWindow is the top-level native window; it owns
// the MainComponent and forwards close requests to the app instead of closing
// silently.

#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace djapp
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name);

    void closeButtonPressed() override;
};

} // namespace djapp
