#pragma once

// model/ — pure data types (Types.h) plus wire (de)serialization (this file) and
// range clamping (Ranges.h). No I/O, no threading of its own: parsing here only
// ever turns a juce::var into a struct or back, nothing touches a socket or a file.
//
// Parsing is strict: unknown fields, wrong JSON types, and non-finite numbers are
// parse failures. Out-of-range-but-well-typed values are NOT parse failures —
// range enforcement is Ranges::clamp()'s job, applied by the caller after a
// successful parse (see docs/plan/02-protocol.md's Field reference).

#include "Types.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace djapp
{

// Success carries a value; failure carries a human-readable reason and a
// default-constructed value. Never partially filled — `value` is either fully
// valid (ok == true) or unused (ok == false).
template <typename T>
struct Result
{
    bool ok = false;
    T value{};
    juce::String error; // empty when ok

    explicit operator bool() const { return ok; }
    const T& operator*() const { return value; }
};

juce::var toVar(const PlaybackState& state);
juce::var toVar(const StateDelta& delta);

// Only PlaybackState and StateDelta are supported; no generic definition is
// provided, so instantiating fromVar<T>() for any other T fails to link.
template <typename T>
Result<T> fromVar(const juce::var& v);

template <>
Result<PlaybackState> fromVar<PlaybackState>(const juce::var& v);

template <>
Result<StateDelta> fromVar<StateDelta>(const juce::var& v);

} // namespace djapp
