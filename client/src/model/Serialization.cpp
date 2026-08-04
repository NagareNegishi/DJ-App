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

    if (!v.isObject())
        return makeFail<R>("expected an object");

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return makeFail<R>("expected an object");

    if (!hasOnlyKnownKeys(*obj,
                           { "trackId", "playing", "positionSeconds", "gain", "playbackRate",
                             "pitchOffsetSemitones", "loop" }))
        return makeFail<R>("unknown field in PlaybackState");

    PlaybackState state;

    if (obj->hasProperty("trackId"))
    {
        const auto& tv = obj->getProperty("trackId");
        if (tv.isVoid())
            state.trackId = {};
        else if (tv.isString())
        {
            auto s = tv.toString();
            if (!isValidTrackId(s))
                return makeFail<R>("invalid trackId");
            state.trackId = s;
        }
        else
            return makeFail<R>("trackId must be a string or null");
    }

    if (obj->hasProperty("playing"))
    {
        const auto& pv = obj->getProperty("playing");
        if (!pv.isBool())
            return makeFail<R>("playing must be a boolean");
        state.playing = static_cast<bool>(pv);
    }

    if (obj->hasProperty("positionSeconds"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("positionSeconds"), d))
            return makeFail<R>("positionSeconds must be a finite number");
        state.positionSeconds = d;
    }

    if (obj->hasProperty("gain"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("gain"), d))
            return makeFail<R>("gain must be a finite number");
        state.gain = static_cast<float>(d);
    }

    if (obj->hasProperty("playbackRate"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("playbackRate"), d))
            return makeFail<R>("playbackRate must be a finite number");
        state.playbackRate = static_cast<float>(d);
    }

    if (obj->hasProperty("pitchOffsetSemitones"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("pitchOffsetSemitones"), d))
            return makeFail<R>("pitchOffsetSemitones must be a finite number");
        state.pitchOffsetSemitones = static_cast<float>(d);
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

    if (!v.isObject())
        return makeFail<R>("expected an object");

    auto* obj = v.getDynamicObject();
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
        const auto& tv = obj->getProperty("trackId");
        if (tv.isVoid())
            delta.trackId = juce::String();
        else if (tv.isString())
        {
            auto s = tv.toString();
            if (!isValidTrackId(s))
                return makeFail<R>("invalid trackId");
            delta.trackId = s;
        }
        else
            return makeFail<R>("trackId must be a string or null");
    }

    if (obj->hasProperty("playing"))
    {
        const auto& pv = obj->getProperty("playing");
        if (!pv.isBool())
            return makeFail<R>("playing must be a boolean");
        delta.playing = static_cast<bool>(pv);
    }

    if (obj->hasProperty("positionSeconds"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("positionSeconds"), d))
            return makeFail<R>("positionSeconds must be a finite number");
        delta.positionSeconds = d;
    }

    if (obj->hasProperty("gain"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("gain"), d))
            return makeFail<R>("gain must be a finite number");
        delta.gain = static_cast<float>(d);
    }

    if (obj->hasProperty("playbackRate"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("playbackRate"), d))
            return makeFail<R>("playbackRate must be a finite number");
        delta.playbackRate = static_cast<float>(d);
    }

    if (obj->hasProperty("pitchOffsetSemitones"))
    {
        double d = 0;
        if (!asFiniteNumber(obj->getProperty("pitchOffsetSemitones"), d))
            return makeFail<R>("pitchOffsetSemitones must be a finite number");
        delta.pitchOffsetSemitones = static_cast<float>(d);
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
