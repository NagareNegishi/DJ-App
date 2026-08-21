#pragma once

// state/ — CrossfaderState: the crossfader's local-only mirror of
// StateManager's listener pattern (one value + listeners), but deliberately
// its own tiny class rather than a StateDelta field. The crossfader never
// leaves this client — it never reaches StateManager or the sync layer — so
// it can't be modeled as synced state; this class exists to give it the same
// shape (value + listener tokens) every other control already has, without
// making it a special case architecturally. Threading: message-thread-only;
// JUCE_ASSERT_MESSAGE_THREAD in every public method.

#include "state/TokenListenerList.h"
#include <functional>

namespace djapp
{

class CrossfaderState
{
  public:
    using Listener = std::function<void(float position)>;

    void setPosition(float position); // clamps to [0,1]; notifies listeners only if
                                       // the clamped value actually changed
    float getPosition() const;        // default 0.5 (center)

    // Token starts at 0, increases monotonically, never reused.
    int addListener(Listener listener);
    void removeListener(int token);

  private:
    float position_ = 0.5f;
    TokenListenerList<Listener> listeners_;
};

} // namespace djapp
