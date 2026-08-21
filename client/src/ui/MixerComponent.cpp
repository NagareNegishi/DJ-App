#include "MixerComponent.h"

namespace djapp
{

MixerComponent::MixerComponent(CrossfaderState& crossfader) : crossfader_(crossfader)
{
    addAndMakeVisible(slider_);
    slider_.setRange(0.0, 1.0);
    slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setValue(crossfader_.getPosition(), juce::dontSendNotification);
    slider_.onValueChange = [this] { crossfader_.setPosition(static_cast<float>(slider_.getValue())); };

    listenerToken_ =
        crossfader_.addListener([this](float position) { slider_.setValue(position, juce::dontSendNotification); });
}

MixerComponent::~MixerComponent()
{
    crossfader_.removeListener(listenerToken_);
}

void MixerComponent::setControlsEnabled(bool enabled)
{
    slider_.setEnabled(enabled);
}

void MixerComponent::resized()
{
    slider_.setBounds(getLocalBounds());
}

} // namespace djapp
