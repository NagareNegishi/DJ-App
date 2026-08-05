#pragma once

// ui/ — dumb view components: render state, emit deltas (from M5 onward).
// TrackListComponent: lists tracks handed to it and exposes which one is
// selected; it does not decide what to do with a selection — that's
// MainComponent's job (M3 dev UI, StateManager-routed deltas from M5).

#include "model/Types.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace djapp
{

class TrackListComponent : public juce::Component, private juce::ListBoxModel
{
  public:
    TrackListComponent();

    void setTracks(std::vector<TrackMetadata> newTracks);

    // nullopt if no row is selected or the selection is out of range of the
    // current tracks (e.g. stale after setTracks shrinks the list).
    std::optional<TrackMetadata> getSelectedTrack() const;

    void resized() override;

  private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override;

    juce::ListBox listBox_{"TrackList", this};
    std::vector<TrackMetadata> tracks_;
};

} // namespace djapp
