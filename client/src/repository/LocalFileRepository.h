#pragma once

// LocalFileRepository — AudioRepository backed by a manifest.json in a local
// directory; every manifest entry is validated (id, path-safety) before it is
// trusted, per docs/plan/06-security.md's "external data never becomes a
// path" rule applied to manifest-declared filenames.

#include "AudioRepository.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace djapp
{

class LocalFileRepository : public AudioRepository
{
  public:
    explicit LocalFileRepository(juce::File rootDir);

    std::vector<TrackMetadata> listAvailableTracks() override;
    std::optional<TrackMetadata> getTrackMetadata(const juce::String& id) override;
    std::shared_ptr<const LoadedAudio> getAudioBuffer(const juce::String& id) override;

  private:
    void loadManifest();
    std::optional<TrackMetadata> parseEntry(const juce::var& entry);

    // Child-of-rootDir and not-a-symlink checks, re-run at every point a file
    // is opened (manifest load and each decode) to close the TOCTOU gap where
    // disk contents change between the two.
    bool isSafeToOpen(const juce::File& file, const juce::String& id) const;

    juce::File rootDir_;
    juce::AudioFormatManager formatManager_;
    std::vector<TrackMetadata> tracks_;                                       // validated at construction
    std::map<juce::String, std::shared_ptr<const LoadedAudio>> decodedCache_; // never re-decode, never mutate
};

} // namespace djapp
