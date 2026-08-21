#pragma once

// model/ - builds a StateDelta carrying every field of a PlaybackState, for a
// caller (app/MainComponent) that needs to push a full resync rather than an
// incremental change: becoming controller doesn't imply the room's canonical
// state already matches what this client was already playing (e.g. solo,
// unsynced, before claiming), so the usual field-at-a-time deltas from later
// UI actions aren't enough to catch peers up.

#include "Types.h"

namespace djapp
{

inline StateDelta fullResyncDelta(DeckId deck, const PlaybackState& state)
{
    StateDelta delta;
    delta.deck = deck;
    delta.trackId = state.trackId;
    delta.playing = state.playing;
    delta.positionSeconds = state.positionSeconds;
    delta.gain = state.gain;
    delta.playbackRate = state.playbackRate;
    delta.pitchOffsetSemitones = state.pitchOffsetSemitones;
    delta.loop = state.loop;
    return delta;
}

} // namespace djapp
