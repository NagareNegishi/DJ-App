#pragma once

// model/ - DeckSyncInfo: everything the sync-button correction needs about
// one deck at the moment of the button press. Used symmetrically for both
// "this" deck and "the other" deck (see ui/DeckComponent.h) so the two sides
// share one type instead of two different shapes.

#include "BeatGrid.h"

namespace djapp
{

struct DeckSyncInfo
{
    BeatGrid beatGrid;
    double positionSeconds = 0.0; // true live engine position - NOT a resume-position
                                  // heuristic (see DeckComponent.h's resumePositionProvider_,
                                  // which stays reserved for the Play button only)
    float playbackRate = 1.0f;
};

} // namespace djapp
