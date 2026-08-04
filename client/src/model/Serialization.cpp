#include "Serialization.h"
#include <cmath>

namespace djapp
{

namespace
{

template <typename T>
Result<T> makeOk(T value)
{
    return Result<T>{ true, std::move(value), {} };
}

template <typename T>
Result<T> makeFail(juce::String message)
{
    return Result<T>{ false, T{}, std::move(message) };
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

    if (!hasOnlyKnownKeys(*obj, { "inSeconds", "outSeconds" }))
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

template <>
Result<PlaybackState> fromVar<PlaybackState>(const juce::var& v)
{
    using R = PlaybackState;

    auto* obj = requireObject(v);
    if (obj == nullptr)
        return makeFail<R>("expected an object");

    if (!hasOnlyKnownKeys(*obj,
                           { "trackId", "playing", "positionSeconds", "gain", "playbackRate",
                             "pitchOffsetSemitones", "loop" }))
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

template <>
Result<StateDelta> fromVar<StateDelta>(const juce::var& v)
{
    using R = StateDelta;

    auto* obj = requireObject(v);
    if (obj == nullptr)
        return makeFail<R>("expected an object");

    if (!hasOnlyKnownKeys(*obj,
                           { "deck", "trackId", "playing", "positionSeconds", "gain", "playbackRate",
                             "pitchOffsetSemitones", "loop" }))
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

} // namespace djapp
