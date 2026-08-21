#pragma once

// model/ - the local-control rule extracted out of app/MainComponent so a
// Catch2 test can pin it directly, since app/ itself is host-checklist-only
// (05-testing.md). Pure booleans in, no Role/PeerInfo/JUCE dependency.

namespace djapp
{

// Solo playback (never connected) is always allowed; once connected, only the
// controller - or anyone, if no one currently controls - may act locally.
inline bool controlsEnabledLocally(bool connected, bool isLocalController, bool anyPeerIsController)
{
    return !connected || isLocalController || !anyPeerIsController;
}

// Shared by every DeckComponent widget so their enabled state can't diverge.
inline bool deckControlEnabled(bool hasTrack, bool rolePermitsControl)
{
    return hasTrack && rolePermitsControl;
}

} // namespace djapp
