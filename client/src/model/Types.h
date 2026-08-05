#pragma once

// model/ — pure data types shared across layers, plus their wire (de)serialization
// in Serialization.h/.cpp and range clamping in Ranges.h. No I/O, no threading of
// its own: every type here is a plain value type safe to pass between threads by
// copy, but nothing in this layer synchronizes access on your behalf.

#include <juce_core/juce_core.h>
#include <optional>

namespace djapp
{

struct TrackMetadata
{
    juce::String id; // ^[A-Za-z0-9._-]{1,64}$
    juce::String title;
    juce::String fileName;      // plain filename, no path separators
    double durationSeconds = 0; // filled after decode; manifest value optional
    std::optional<double> bpm;  // manifest-declared, no detection yet
};

struct LoopPoints
{
    double inSeconds = 0, outSeconds = 0;
};

struct PlaybackState
{
    juce::String trackId; // empty = no track (JSON null)
    bool playing = false;
    double positionSeconds = 0;
    float gain = 1.0f;
    float playbackRate = 1.0f;
    float pitchOffsetSemitones = 0.0f; // stored+synced, not rendered until M9
    std::optional<LoopPoints> loop;
};

enum class DeckId
{
    A,
    B
};

inline juce::String toString(DeckId deck)
{
    return deck == DeckId::A ? "A" : "B";
}

inline std::optional<DeckId> fromString(const juce::String& s)
{
    if (s == "A")
        return DeckId::A;
    if (s == "B")
        return DeckId::B;
    return std::nullopt;
}

struct StateDelta
{
    DeckId deck = DeckId::A;
    std::optional<juce::String> trackId; // nullopt = field absent; empty string = explicit null
    std::optional<bool> playing;
    std::optional<double> positionSeconds;
    std::optional<float> gain;
    std::optional<float> playbackRate;
    std::optional<float> pitchOffsetSemitones;
    std::optional<std::optional<LoopPoints>> loop; // outer=absent?, inner=null vs value

    bool empty() const
    {
        return !trackId.has_value() && !playing.has_value() && !positionSeconds.has_value() && !gain.has_value() &&
               !playbackRate.has_value() && !pitchOffsetSemitones.has_value() && !loop.has_value();
    }
};

} // namespace djapp
