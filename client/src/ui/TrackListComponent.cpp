#include "TrackListComponent.h"

namespace djapp
{

TrackListComponent::TrackListComponent()
{
    addAndMakeVisible(listBox_);
}

void TrackListComponent::setTracks(std::vector<TrackMetadata> newTracks)
{
    tracks_ = std::move(newTracks);
    listBox_.updateContent();
    listBox_.repaint();
}

std::optional<TrackMetadata> TrackListComponent::getSelectedTrack() const
{
    const int row = listBox_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(tracks_.size()))
        return std::nullopt;

    return tracks_[static_cast<size_t>(row)];
}

void TrackListComponent::resized()
{
    listBox_.setBounds(getLocalBounds());
}

int TrackListComponent::getNumRows()
{
    return static_cast<int>(tracks_.size());
}

void TrackListComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    if (rowNumber < 0 || rowNumber >= static_cast<int>(tracks_.size()))
        return;

    const auto& track = tracks_[static_cast<size_t>(rowNumber)];
    g.setColour(juce::Colours::black);
    g.drawText(track.title + " (" + track.id + ")", 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void TrackListComponent::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(tracks_.size()))
        return;

    juce::Logger::writeToLog("TrackListComponent: selected track \"" + tracks_[static_cast<size_t>(row)].id + "\"");
}

void TrackListComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent& event)
{
    listBoxItemClicked(row, event);
}

} // namespace djapp
