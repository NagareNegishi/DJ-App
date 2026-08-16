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
template <typename T> struct Result
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
template <typename T> Result<T> fromVar(const juce::var& v);

template <> Result<PlaybackState> fromVar<PlaybackState>(const juce::var& v);

template <> Result<StateDelta> fromVar<StateDelta>(const juce::var& v);

// Whole-protocol-message (de)serialization: everything below builds or parses a
// complete `{type: "...", ...}` wire message, as opposed to the field-level
// (de)serialization above. Pure — no I/O, no IXWebSocket — consumed by
// sync/WebSocketTransport (M7).

constexpr int kProtocolVersion = 1; // shared/protocol/PROTOCOL-VERSION

// Validates `name`/`room` against 02-protocol.md's `hello` field constraints
// (`name`: string 1-32, printable, no control chars; `room`: string 1-64)
// before building the envelope. Failure means the caller must never attempt
// to connect with this input.
Result<juce::var> buildHello(const juce::String& name, const juce::String& room);

juce::var buildDelta(const StateDelta& delta); // {type:"delta", deck, changes:{...}}
juce::var buildClaimControl();
juce::var buildReleaseControl();
juce::var buildRequestSnapshot();

// "" if `message` is not an object or its "type" is missing/not a string.
juce::String messageType(const juce::var& message);

// message must already have type == "delta". Reconstructs a flat {deck, ...changes}
// var from the wire's {deck, changes:{...}} shape and parses it via fromVar<StateDelta>.
// Fails if `deck` is absent or `changes` is not an object (mirrors the retired
// flattenWireDelta's exact two failure conditions).
Result<StateDelta> parseDeltaMessage(const juce::var& message);

// Reads `serverSeq` off any server-to-client message that carries one (welcome, delta,
// snapshot). std::nullopt if absent or not a non-negative integer-valued number.
std::optional<int> messageServerSeq(const juce::var& message);

// Maps a WebSocket close code (02-protocol.md's Error codes / close code table) to one
// human-readable string for SyncTransport::Callbacks::onConnectionChange. Deliberately
// takes only the code, not the server's free-text close reason: every documented code
// already has one fixed, complete meaning, and the server's own accompanying string is
// not something the client has any protocol reason to trust (log-line forging via
// embedded newlines, a crafted "reason" coaxing a user toward something outside the
// app). Every documented code (4000-4004, 1008, 1009, 1001, 1011) gets a specific
// description; an undocumented code still returns a safe generic string.
juce::String describeCloseCode(int code);

// Serializes `message` and checks it against the 4096-byte wire limit (02-protocol.md
// Transport section). Failure means "too large to send" — the caller must drop, never
// truncate or send anyway. This is the one place the byte-length check happens; both
// the handshake's hello and every SyncTransport::send* method route through it, so the
// check cannot be bypassed by a new call site later forgetting it.
Result<juce::String> serializeForSend(const juce::var& message);

} // namespace djapp
