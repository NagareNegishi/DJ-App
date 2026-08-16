#include "Serialization.h"
#include <cmath>
#include <limits>

namespace djapp
{

namespace
{

template <typename T> Result<T> makeOk(T value)
{
    return Result<T>{true, std::move(value), {}};
}

template <typename T> Result<T> makeFail(juce::String message)
{
    return Result<T>{false, T{}, std::move(message)};
}

bool isValidTrackId(const juce::String& s)
{
    if (s.isEmpty() || s.length() > 64)
        return false;

    if (!s.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-"))
        return false;

    // Reject "." / ".." and any embedded ".." even though the charset above
    // would otherwise accept them — trackId must never be usable as a path segment.
    if (s == "." || s == ".." || s.contains(".."))
        return false;

    return true;
}

// 02-protocol.md's `hello` fields: "printable, no control chars". Rejects C0
// controls, DEL, and the C1 block; does not attempt full Unicode category
// matching (e.g. bidi overrides) the way the server's regex does — this is a
// pre-send sanity check, not the security boundary (the server re-validates
// every hello independently).
bool isPrintableNoControlChars(const juce::String& s)
{
    for (auto c : s)
        if (c <= 0x1F || (c >= 0x7F && c <= 0x9F))
            return false;
    return true;
}

bool isValidHelloName(const juce::String& name)
{
    return name.isNotEmpty() && name.length() <= 32 && isPrintableNoControlChars(name);
}

bool isValidHelloRoom(const juce::String& room)
{
    return room.isNotEmpty() && room.length() <= 64;
}

bool asFiniteNumber(const juce::var& v, double& out)
{
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;

    const double d = static_cast<double>(v);
    if (!std::isfinite(d))
        return false;

    out = d;
    return true;
}

// Validates as a finite double, then narrows to float and re-checks finiteness:
// a finite double outside float range (e.g. 1e300) narrows to +/-Infinity, so the
// double-only finiteness check above isn't sufficient once the result is stored
// in a float field.
bool asFiniteFloat(const juce::var& v, float& out)
{
    double d = 0;
    if (!asFiniteNumber(v, d))
        return false;

    const float f = static_cast<float>(d);
    if (!std::isfinite(f))
        return false;

    out = f;
    return true;
}

// Returns the object's DynamicObject if `v` is a JSON object, nullptr otherwise.
// Callers produce their own ("expected an object") failure on nullptr since the
// error message and Result<T> type differ per call site.
const juce::DynamicObject* requireObject(const juce::var& v)
{
    if (!v.isObject())
        return nullptr;
    return v.getDynamicObject();
}

// Shared per-field value parsers used by both fromVar<PlaybackState> and
// fromVar<StateDelta>: each call site still does its own hasProperty(...) presence
// check and assigns the result directly (PlaybackState) or into a std::optional
// (StateDelta) — assignment already works identically either way.
Result<bool> parseBool(const juce::var& v)
{
    if (!v.isBool())
        return makeFail<bool>("playing must be a boolean");
    return makeOk<bool>(static_cast<bool>(v));
}

Result<double> parseFinite(const juce::var& v, const char* field)
{
    double d = 0;
    if (!asFiniteNumber(v, d))
        return makeFail<double>(juce::String(field) + " must be a finite number");
    return makeOk<double>(d);
}

Result<float> parseFiniteFloat(const juce::var& v, const char* field)
{
    float f = 0;
    if (!asFiniteFloat(v, f))
        return makeFail<float>(juce::String(field) + " must be a finite number");
    return makeOk<float>(f);
}

Result<juce::String> parseTrackId(const juce::var& v)
{
    if (v.isVoid())
        return makeOk<juce::String>(juce::String());
    if (!v.isString())
        return makeFail<juce::String>("trackId must be a string or null");
    auto s = v.toString();
    if (!isValidTrackId(s))
        return makeFail<juce::String>("invalid trackId");
    return makeOk<juce::String>(s);
}

bool hasOnlyKnownKeys(const juce::DynamicObject& obj, std::initializer_list<const char*> allowed)
{
    for (auto& prop : obj.getProperties())
    {
        bool known = false;
        for (auto* name : allowed)
        {
            if (prop.name == juce::Identifier(name))
            {
                known = true;
                break;
            }
        }
        if (!known)
            return false;
    }
    return true;
}

// Parses a `loop` field value shared by PlaybackState and StateDelta: JSON null
// means "no loop" (nullopt); a well-formed {inSeconds, outSeconds} object with
// inSeconds < outSeconds succeeds; anything else fails. The ordering check can't
// be repaired later by Ranges::clamp() (it clamps each endpoint independently),
// so it must be rejected here at parse time rather than left to range clamping.
Result<std::optional<LoopPoints>> parseLoopValue(const juce::var& v)
{
    using R = std::optional<LoopPoints>;

    if (v.isVoid())
        return makeOk<R>(std::nullopt);

    if (!v.isObject())
        return makeFail<R>("loop must be an object or null");

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return makeFail<R>("loop must be an object or null");

    if (!hasOnlyKnownKeys(*obj, {"inSeconds", "outSeconds"}))
        return makeFail<R>("loop has unknown field");

    if (!obj->hasProperty("inSeconds") || !obj->hasProperty("outSeconds"))
        return makeFail<R>("loop missing inSeconds/outSeconds");

    double inSeconds = 0, outSeconds = 0;
    if (!asFiniteNumber(obj->getProperty("inSeconds"), inSeconds))
        return makeFail<R>("loop.inSeconds must be a finite number");
    if (!asFiniteNumber(obj->getProperty("outSeconds"), outSeconds))
        return makeFail<R>("loop.outSeconds must be a finite number");

    if (!(inSeconds < outSeconds))
        return makeFail<R>("loop.inSeconds must be less than loop.outSeconds");

    LoopPoints loop;
    loop.inSeconds = inSeconds;
    loop.outSeconds = outSeconds;
    return makeOk<R>(loop);
}

juce::var loopToVar(const LoopPoints& loop)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("inSeconds", loop.inSeconds);
    obj->setProperty("outSeconds", loop.outSeconds);
    return juce::var(obj.get());
}

} // namespace

juce::var toVar(const PlaybackState& state)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("trackId", state.trackId.isEmpty() ? juce::var() : juce::var(state.trackId));
    obj->setProperty("playing", state.playing);
    obj->setProperty("positionSeconds", state.positionSeconds);
    obj->setProperty("gain", static_cast<double>(state.gain));
    obj->setProperty("playbackRate", static_cast<double>(state.playbackRate));
    obj->setProperty("pitchOffsetSemitones", static_cast<double>(state.pitchOffsetSemitones));
    obj->setProperty("loop", state.loop.has_value() ? loopToVar(*state.loop) : juce::var());
    return juce::var(obj.get());
}

juce::var toVar(const StateDelta& delta)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("deck", toString(delta.deck));

    if (delta.trackId.has_value())
        obj->setProperty("trackId", delta.trackId->isEmpty() ? juce::var() : juce::var(*delta.trackId));
    if (delta.playing.has_value())
        obj->setProperty("playing", *delta.playing);
    if (delta.positionSeconds.has_value())
        obj->setProperty("positionSeconds", *delta.positionSeconds);
    if (delta.gain.has_value())
        obj->setProperty("gain", static_cast<double>(*delta.gain));
    if (delta.playbackRate.has_value())
        obj->setProperty("playbackRate", static_cast<double>(*delta.playbackRate));
    if (delta.pitchOffsetSemitones.has_value())
        obj->setProperty("pitchOffsetSemitones", static_cast<double>(*delta.pitchOffsetSemitones));
    if (delta.loop.has_value())
        obj->setProperty("loop", delta.loop->has_value() ? loopToVar(**delta.loop) : juce::var());

    return juce::var(obj.get());
}

template <> Result<PlaybackState> fromVar<PlaybackState>(const juce::var& v)
{
    using R = PlaybackState;

    auto* obj = requireObject(v);
    if (obj == nullptr)
        return makeFail<R>("expected an object");

    if (!hasOnlyKnownKeys(
            *obj, {"trackId", "playing", "positionSeconds", "gain", "playbackRate", "pitchOffsetSemitones", "loop"}))
        return makeFail<R>("unknown field in PlaybackState");

    PlaybackState state;

    if (obj->hasProperty("trackId"))
    {
        auto r = parseTrackId(obj->getProperty("trackId"));
        if (!r)
            return makeFail<R>(r.error);
        state.trackId = *r;
    }

    if (obj->hasProperty("playing"))
    {
        auto r = parseBool(obj->getProperty("playing"));
        if (!r)
            return makeFail<R>(r.error);
        state.playing = *r;
    }

    if (obj->hasProperty("positionSeconds"))
    {
        auto r = parseFinite(obj->getProperty("positionSeconds"), "positionSeconds");
        if (!r)
            return makeFail<R>(r.error);
        state.positionSeconds = *r;
    }

    if (obj->hasProperty("gain"))
    {
        auto r = parseFiniteFloat(obj->getProperty("gain"), "gain");
        if (!r)
            return makeFail<R>(r.error);
        state.gain = *r;
    }

    if (obj->hasProperty("playbackRate"))
    {
        auto r = parseFiniteFloat(obj->getProperty("playbackRate"), "playbackRate");
        if (!r)
            return makeFail<R>(r.error);
        state.playbackRate = *r;
    }

    if (obj->hasProperty("pitchOffsetSemitones"))
    {
        auto r = parseFiniteFloat(obj->getProperty("pitchOffsetSemitones"), "pitchOffsetSemitones");
        if (!r)
            return makeFail<R>(r.error);
        state.pitchOffsetSemitones = *r;
    }

    if (obj->hasProperty("loop"))
    {
        auto loopResult = parseLoopValue(obj->getProperty("loop"));
        if (!loopResult.ok)
            return makeFail<R>(loopResult.error);
        state.loop = *loopResult;
    }

    return makeOk<R>(state);
}

template <> Result<StateDelta> fromVar<StateDelta>(const juce::var& v)
{
    using R = StateDelta;

    auto* obj = requireObject(v);
    if (obj == nullptr)
        return makeFail<R>("expected an object");

    if (!hasOnlyKnownKeys(*obj, {"deck", "trackId", "playing", "positionSeconds", "gain", "playbackRate",
                                 "pitchOffsetSemitones", "loop"}))
        return makeFail<R>("unknown field in StateDelta");

    if (!obj->hasProperty("deck"))
        return makeFail<R>("deck is required");

    const auto& deckVar = obj->getProperty("deck");
    if (!deckVar.isString())
        return makeFail<R>("deck must be a string");

    auto deck = fromString(deckVar.toString());
    if (!deck.has_value())
        return makeFail<R>("deck must be \"A\" or \"B\"");

    StateDelta delta;
    delta.deck = *deck;

    if (obj->hasProperty("trackId"))
    {
        auto r = parseTrackId(obj->getProperty("trackId"));
        if (!r)
            return makeFail<R>(r.error);
        delta.trackId = *r;
    }

    if (obj->hasProperty("playing"))
    {
        auto r = parseBool(obj->getProperty("playing"));
        if (!r)
            return makeFail<R>(r.error);
        delta.playing = *r;
    }

    if (obj->hasProperty("positionSeconds"))
    {
        auto r = parseFinite(obj->getProperty("positionSeconds"), "positionSeconds");
        if (!r)
            return makeFail<R>(r.error);
        delta.positionSeconds = *r;
    }

    if (obj->hasProperty("gain"))
    {
        auto r = parseFiniteFloat(obj->getProperty("gain"), "gain");
        if (!r)
            return makeFail<R>(r.error);
        delta.gain = *r;
    }

    if (obj->hasProperty("playbackRate"))
    {
        auto r = parseFiniteFloat(obj->getProperty("playbackRate"), "playbackRate");
        if (!r)
            return makeFail<R>(r.error);
        delta.playbackRate = *r;
    }

    if (obj->hasProperty("pitchOffsetSemitones"))
    {
        auto r = parseFiniteFloat(obj->getProperty("pitchOffsetSemitones"), "pitchOffsetSemitones");
        if (!r)
            return makeFail<R>(r.error);
        delta.pitchOffsetSemitones = *r;
    }

    if (obj->hasProperty("loop"))
    {
        auto loopResult = parseLoopValue(obj->getProperty("loop"));
        if (!loopResult.ok)
            return makeFail<R>(loopResult.error);
        delta.loop = *loopResult;
    }

    return makeOk<R>(delta);
}

Result<juce::var> buildHello(const juce::String& name, const juce::String& room)
{
    if (!isValidHelloName(name))
        return makeFail<juce::var>("hello name must be 1-32 printable characters with no control characters");

    if (!isValidHelloRoom(room))
        return makeFail<juce::var>("hello room must be 1-64 characters");

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "hello");
    obj->setProperty("protocolVersion", kProtocolVersion);
    obj->setProperty("name", name);
    obj->setProperty("room", room);
    return makeOk<juce::var>(juce::var(obj.get()));
}

juce::var buildDelta(const StateDelta& delta)
{
    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("type", "delta");

    juce::DynamicObject::Ptr changes = new juce::DynamicObject();

    // toVar(delta) already produces the flat {deck, ...present fields} shape;
    // `deck` moves to the envelope's top level, everything else nests under
    // "changes" to match the wire shape (shared/protocol/fixtures/
    // client-to-server/valid/delta-*.json). Bound to a named local: a temporary
    // here would be destroyed at the end of this declaration (before the loop
    // below runs), leaving flatObj dangling.
    const juce::var flat = toVar(delta);
    if (auto* flatObj = flat.getDynamicObject())
    {
        for (auto& prop : flatObj->getProperties())
        {
            if (prop.name == juce::Identifier("deck"))
                result->setProperty(prop.name, prop.value);
            else
                changes->setProperty(prop.name, prop.value);
        }
    }

    result->setProperty("changes", juce::var(changes.get()));
    return juce::var(result.get());
}

juce::var buildClaimControl()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "claimControl");
    return juce::var(obj.get());
}

juce::var buildReleaseControl()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "releaseControl");
    return juce::var(obj.get());
}

juce::var buildRequestSnapshot()
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", "requestSnapshot");
    return juce::var(obj.get());
}

juce::String messageType(const juce::var& message)
{
    auto* obj = requireObject(message);
    if (obj == nullptr || !obj->hasProperty("type"))
        return {};

    const auto typeVar = obj->getProperty("type");
    if (!typeVar.isString())
        return {};

    return typeVar.toString();
}

Result<StateDelta> parseDeltaMessage(const juce::var& message)
{
    auto* obj = requireObject(message);
    if (obj == nullptr || !obj->hasProperty("deck"))
        return makeFail<StateDelta>("delta message is missing deck");

    auto* changesObj = obj->getProperty("changes").getDynamicObject();
    if (changesObj == nullptr)
        return makeFail<StateDelta>("delta message's changes must be an object");

    // Reads exactly `deck` and the keys of `changes` — nothing else off the incoming
    // message — so no extra top-level field (serverSeq, sourceClientId, ...) can leak
    // into the flattened object; fromVar<StateDelta> strictly rejects unknown keys.
    juce::DynamicObject::Ptr flattened = new juce::DynamicObject();
    flattened->setProperty("deck", obj->getProperty("deck"));

    for (auto& prop : changesObj->getProperties())
        flattened->setProperty(prop.name, prop.value);

    return fromVar<StateDelta>(juce::var(flattened.get()));
}

std::optional<int> messageServerSeq(const juce::var& message)
{
    auto* obj = requireObject(message);
    if (obj == nullptr || !obj->hasProperty("serverSeq"))
        return std::nullopt;

    double d = 0;
    if (!asFiniteNumber(obj->getProperty("serverSeq"), d))
        return std::nullopt;

    if (d < 0 || std::floor(d) != d || d > static_cast<double>(std::numeric_limits<int>::max()))
        return std::nullopt;

    return static_cast<int>(d);
}

juce::String describeCloseCode(int code)
{
    switch (code)
    {
    case 4000:
        return "handshake failed";
    case 4001:
        return "protocol version mismatch";
    case 4002:
        return "room is full";
    case 4003:
        return "disconnected for sending too many messages";
    case 4004:
        return "wrong room code";
    case 1001:
        return "server is shutting down";
    case 1008:
        return "disconnected: protocol violation";
    case 1009:
        return "disconnected: message too large";
    case 1011:
        return "server encountered an internal error";
    default:
        return "connection closed";
    }
}

Result<juce::String> serializeForSend(const juce::var& message)
{
    const auto text = juce::JSON::toString(message, true);
    if (text.getNumBytesAsUTF8() > 4096)
        return makeFail<juce::String>("message exceeds the 4096-byte wire limit");

    return makeOk<juce::String>(text);
}

} // namespace djapp
