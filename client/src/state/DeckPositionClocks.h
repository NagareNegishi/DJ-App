#pragma once

// state/ — DeckPositionClocks: closes the class of bug where a per-deck
// fan-out call site forgets one deck. PositionClock is single-deck by design
// (see state/PositionClock.h); SyncPublisher already solves this same shape
// of problem by being internally multi-deck (sync/SyncPublisher.h,
// kDeckCount = 2). This wrapper gets PositionClock the same guarantee
// without redesigning it: callers make one setRole() call instead of
// hand-duplicating it per deck, so it's no longer possible to update deck A
// and forget deck B.

#include "state/PositionClock.h" // reuse Role — do not redeclare it, do not include sync/SyncPublisher.h directly

namespace djapp
{

class DeckPositionClocks
{
  public:
    DeckPositionClocks(PositionClock& clockA, PositionClock& clockB);

    void setRole(Role role); // calls clockA.setRole(role) then clockB.setRole(role)

  private:
    PositionClock& clockA_;
    PositionClock& clockB_;
};

} // namespace djapp
