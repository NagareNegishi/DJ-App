#pragma once

// ui/ — MixerComponent: the crossfader slider. Deliberately local-only, like
// the CrossfaderState it drives (see state/CrossfaderState.h) — nothing here
// ever reaches StateManager or the sync layer, so this control looks and
// behaves the same for every connected user, each sweeping their own local
// mix independently.

#include "state/CrossfaderState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace djapp
{

class MixerComponent : public juce::Component
{
  public:
    explicit MixerComponent(CrossfaderState& crossfader);

    void setControlsEnabled(bool enabled); // role-based gate, mirrors DeckComponent::setControlsEnabled

    void resized() override;

  private:
    CrossfaderState& crossfader_;
    juce::Slider slider_;
};

} // namespace djapp
