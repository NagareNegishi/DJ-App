#include "DeckPositionClocks.h"

namespace djapp
{

DeckPositionClocks::DeckPositionClocks(PositionClock& clockA, PositionClock& clockB) : clockA_(clockA), clockB_(clockB)
{
}

void DeckPositionClocks::setRole(Role role)
{
    clockA_.setRole(role);
    clockB_.setRole(role);
}

} // namespace djapp
