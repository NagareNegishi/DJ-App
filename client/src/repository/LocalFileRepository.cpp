#include "LocalFileRepository.h"
#include <cmath>
#include <exception>
#include <juce_events/juce_events.h>

namespace djapp
{

namespace
{

// ~1 hour stereo @ 48kHz (lengthInSamples * numChannels); a generous bound for
// this prototype's own demo tracks, meant only to stop an oversized/corrupt
// file from crashing the decode via a runaway allocation.
constexpr juce::int64 maxDecodedSampleCount = 48000LL * 3600LL * 2LL;

bool isValidTrackId(const juce::String& s)
{
    if (s.isEmpty() || s.length() > 64)
        return false;
    return s.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-");
}

// Manifest "file" values must be a bare filename: no directory separators,
// no "..". The isAChildOf/isSymbolicLink checks in parseEntry() are defense
// in depth on top of this — this check alone should already be sufficient.
bool isBareFilename(const juce::String& s)
{
    if (s.isEmpty())
        return false;
    return !s.contains("/") && !s.contains("\\") && !s.contains("..");
}

} // namespace

bool LocalFileRepository::isSafeToOpen(const juce::File& file, const juce::String& id) const
{
    if (!file.isAChildOf(rootDir_))
    {
        juce::Logger::writeToLog("LocalFileRepository: rejecting \"" + id + "\": resolved path escapes root");
        return false;
    }

    if (file.isSymbolicLink())
    {
        juce::Logger::writeToLog("LocalFileRepository: rejecting \"" + id + "\": file is a symbolic link");
        return false;
    }

    return true;
}

LocalFileRepository::LocalFileRepository(juce::File rootDir) : rootDir_(std::move(rootDir))
{
    JUCE_ASSERT_MESSAGE_THREAD

    formatManager_.registerBasicFormats();
    loadManifest();
}

void LocalFileRepository::loadManifest()
{
    tracks_.clear();

    auto manifestFile = rootDir_.getChildFile("manifest.json");
    if (!manifestFile.existsAsFile())
    {
        juce::Logger::writeToLog("LocalFileRepository: no manifest.json at " + manifestFile.getFullPathName());
        return;
    }

    auto parsed = juce::JSON::parse(manifestFile);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("tracks") || !obj->getProperty("tracks").isArray())
    {
        juce::Logger::writeToLog("LocalFileRepository: manifest.json is missing or malformed");
        return;
    }

    for (auto& entry : *obj->getProperty("tracks").getArray())
        if (auto meta = parseEntry(entry))
            tracks_.push_back(*meta);
}

std::optional<TrackMetadata> LocalFileRepository::parseEntry(const juce::var& entry)
{
    auto* obj = entry.isObject() ? entry.getDynamicObject() : nullptr;
    if (obj == nullptr)
    {
        juce::Logger::writeToLog("LocalFileRepository: skipping manifest entry: not an object");
        return std::nullopt;
    }

    auto idVar = obj->getProperty("id");
    if (!idVar.isString() || !isValidTrackId(idVar.toString()))
    {
        juce::Logger::writeToLog("LocalFileRepository: skipping manifest entry: invalid or missing id");
        return std::nullopt;
    }
    const auto id = idVar.toString();

    auto titleVar = obj->getProperty("title");
    if (!titleVar.isString())
    {
        juce::Logger::writeToLog("LocalFileRepository: skipping entry \"" + id + "\": invalid or missing title");
        return std::nullopt;
    }

    auto fileVar = obj->getProperty("file");
    if (!fileVar.isString() || !isBareFilename(fileVar.toString()))
    {
        juce::Logger::writeToLog("LocalFileRepository: skipping entry \"" + id + "\": invalid or missing file name");
        return std::nullopt;
    }
    const auto fileName = fileVar.toString();

    auto file = rootDir_.getChildFile(fileName);

    // Textual child check is defense in depth on top of isBareFilename() above;
    // the symlink check closes the gap it can't see through: a symlink whose
    // target escapes rootDir but whose own path still looks like a child.
    if (!isSafeToOpen(file, id))
        return std::nullopt;

    // Cheap header-only open: reads lengthInSamples/sampleRate, not PCM data.
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr)
    {
        juce::Logger::writeToLog("LocalFileRepository: skipping entry \"" + id + "\": cannot open audio file");
        return std::nullopt;
    }

    TrackMetadata meta;
    meta.id = id;
    meta.title = titleVar.toString();
    meta.fileName = fileName;
    if (reader->sampleRate > 0)
        meta.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

    if (obj->hasProperty("bpm"))
    {
        auto bpmVar = obj->getProperty("bpm");
        if (bpmVar.isDouble() || bpmVar.isInt() || bpmVar.isInt64())
        {
            const double bpm = static_cast<double>(bpmVar);
            if (std::isfinite(bpm) && bpm > 0)
                meta.bpm = bpm;
        }
    }

    return meta;
}

std::vector<TrackMetadata> LocalFileRepository::listAvailableTracks()
{
    JUCE_ASSERT_MESSAGE_THREAD

    return tracks_;
}

std::optional<TrackMetadata> LocalFileRepository::getTrackMetadata(const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto& track : tracks_)
        if (track.id == id)
            return track;
    return std::nullopt;
}

std::shared_ptr<const LoadedAudio> LocalFileRepository::getAudioBuffer(const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto it = decodedCache_.find(id); it != decodedCache_.end())
        return it->second;

    auto meta = getTrackMetadata(id);
    if (!meta.has_value())
    {
        juce::Logger::writeToLog("LocalFileRepository: getAudioBuffer: unknown track id \"" + id + "\"");
        return nullptr;
    }

    auto file = rootDir_.getChildFile(meta->fileName);

    // Re-check even though parseEntry() already validated this filename at
    // manifest load: closes the TOCTOU window where disk contents change
    // between load and decode (e.g. the file gets replaced by a symlink).
    if (!isSafeToOpen(file, id))
        return nullptr;

    try
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
        if (reader == nullptr)
        {
            juce::Logger::writeToLog("LocalFileRepository: getAudioBuffer: cannot open \"" + id + "\"");
            return nullptr;
        }

        const auto numChannels = static_cast<juce::int64>(reader->numChannels);
        const auto totalSamples = reader->lengthInSamples * numChannels;
        if (reader->lengthInSamples <= 0 || totalSamples > maxDecodedSampleCount)
        {
            juce::Logger::writeToLog("LocalFileRepository: getAudioBuffer: \"" + id +
                                     "\" exceeds decode size limit or is empty");
            return nullptr;
        }

        auto audio = std::make_shared<LoadedAudio>();
        audio->sampleRate = reader->sampleRate;
        audio->buffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&audio->buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        decodedCache_[id] = audio;
        return audio;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog(juce::String("LocalFileRepository: getAudioBuffer: exception decoding \"") + id +
                                 "\": " + e.what());
        return nullptr;
    }
}

} // namespace djapp
