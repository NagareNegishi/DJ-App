#pragma once

// ui/ — dumb view components: render state, emit deltas (from M5 onward).
// TrackListComponent (M2): lists tracks handed to it; a row click just logs
// the selected track id — no engine/state access, no deltas yet.

#include "model/Types.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace djapp
{

class TrackListComponent : public juce::Component, private juce::ListBoxModel
{
public:
    TrackListComponent();

    void setTracks(std::vector<TrackMetadata> newTracks);

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;

    juce::ListBox listBox_{ "TrackList", this };
    std::vector<TrackMetadata> tracks_;
};

} // namespace djapp
