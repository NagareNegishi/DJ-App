# 04 — Client (C++ / JUCE 8)

Layer-by-layer specification. Signatures below are the contract; rename locals/privates freely, but keep public shapes, ownership, and threading rules. All headers use `#pragma once`. Namespace everything under `djapp`.

## CMake changes (extends existing `client/CMakeLists.txt`)

- Keep the existing `dj-app-client` GUI target and its structure/comments; add sources as they appear.
- Add modules to `target_link_libraries` as milestones need them: `juce::juce_audio_utils` (pulls in audio_basics, audio_devices, audio_formats) at M2/M3; keep `juce_gui_basics`.
- Add IXWebSocket at M7 via `FetchContent` (pinned tag, `USE_TLS=OFF` for prototype), linked **only** into the target; wrapped entirely inside `sync/WebSocketTransport.cpp`.
- Add a `dj-app-tests` console target (Catch2 v3 via FetchContent, pinned) at M0: compiles files from `src/model`, `src/state`, `src/engine` (pure parts) plus `tests/`; registers with CTest via `catch_discover_tests`. GUI app modules are not linked into tests except the juce core/audio modules the tested code needs (`juce::juce_audio_basics`, `juce::juce_core`, `juce::juce_data_structures` for `juce::var`/JSON).
- `option(DJAPP_BUILD_TESTS "Build unit tests" ON)` so the Windows host can skip tests if desired.

## `model/` — pure data (M1)

```cpp
struct TrackMetadata {
    juce::String id;            // ^[A-Za-z0-9._-]{1,64}$
    juce::String title;
    juce::String fileName;      // plain filename, no path separators
    double durationSeconds = 0; // filled after decode; manifest value optional
    std::optional<double> bpm;  // manifest-declared, no detection yet
};

struct LoopPoints { double inSeconds = 0, outSeconds = 0; };

struct PlaybackState {
    juce::String trackId;                 // empty = no track (JSON null)
    bool playing = false;
    double positionSeconds = 0;
    float gain = 1.0f;
    float playbackRate = 1.0f;
    float pitchOffsetSemitones = 0.0f;    // stored+synced, not rendered until M9
    std::optional<LoopPoints> loop;
};

enum class DeckId { A, B };               // toString "A"/"B", fromString

struct StateDelta {                        // partial PlaybackState
    DeckId deck = DeckId::A;
    std::optional<juce::String> trackId;  // nullopt = field absent; empty string = explicit null
    std::optional<bool> playing;
    std::optional<double> positionSeconds;
    std::optional<float> gain;
    std::optional<float> playbackRate;
    std::optional<float> pitchOffsetSemitones;
    std::optional<std::optional<LoopPoints>> loop;  // outer=absent?, inner=null vs value
    bool empty() const;
};
```

`model/Serialization.h/.cpp`: `juce::var toVar(...)` / `Result<T> fromVar(const juce::var&)` for `PlaybackState`, `StateDelta`, and whole protocol messages. Use `juce::JSON::parse/toString` — **no third-party JSON library**. Parsing is strict per `02-protocol.md`: reject unknown fields, wrong types, non-finite numbers, out-of-range values (return failure, never partially fill). `model/Ranges.h`: one place holding the min/max constants from the Field reference plus `clamp(PlaybackState&)` / `clamp(StateDelta&)` helpers.

## `repository/` — AudioRepository (M2)

```cpp
struct LoadedAudio {
    juce::AudioBuffer<float> buffer;   // whole track in memory (DJ-standard: instant seek)
    double sampleRate = 0;
};

class AudioRepository {
public:
    virtual ~AudioRepository() = default;
    virtual std::vector<TrackMetadata> listAvailableTracks() = 0;
    virtual std::optional<TrackMetadata> getTrackMetadata(const juce::String& id) = 0;
    virtual std::shared_ptr<const LoadedAudio> getAudioBuffer(const juce::String& id) = 0; // nullptr on failure
};
```

`LocalFileRepository(rootDir)`: reads `<rootDir>/manifest.json`:
```json
{ "tracks": [ { "id": "demo1", "title": "Demo One", "file": "demo1.wav", "bpm": 120 } ] }
```
Rules: validate every entry (id regex, `file` must be a bare filename — reject anything containing `/`, `\`, or `..`; resolved file must be a child of `rootDir` — check with `juce::File::isAChildOf`). Invalid entries are skipped with a log line, not fatal. Decode via `juce::AudioFormatManager::registerBasicFormats()` → `createReaderFor` → read fully into `AudioBuffer<float>`; store the reader's sample rate; fill `durationSeconds` from decoded length. Cache decoded tracks in a map of `shared_ptr<const LoadedAudio>` (never re-decode; never mutate a published buffer). Commit `manifest.example.json`; gitignore `client/assets/tracks/*` except the example. Loading runs off the message thread is **not** required for prototype — a synchronous load with a brief UI "loading…" note is acceptable (record in DEVIATIONS if you make it async).

## `engine/` — AudioEngine (M3)

```cpp
class AudioEngine {
public:
    virtual ~AudioEngine() = default;
    virtual void load(std::shared_ptr<const LoadedAudio> audio) = 0; // resets position to 0, keeps gain/rate
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void setGain(float linearGain) = 0;
    virtual void setPlaybackRate(float rate) = 0;
    virtual void setLoop(std::optional<LoopPoints> loop) = 0;
    virtual double getCurrentPosition() const = 0;
    virtual bool isPlaying() const = 0;
};
```

Two pieces:

1. **`BufferPlaybackSource : juce::AudioSource`** — the pure, unit-testable DSP core. Owns: `shared_ptr<const LoadedAudio>` (swapped only via the atomic-publish pattern from `01-architecture.md` Threading), `std::atomic<bool> playing`, `std::atomic<double> positionSamples`, `std::atomic<float> gain, rate`, atomic loop points. `getNextAudioBlock`: if not playing → clear buffer; else advance read head by `rate × (sourceSampleRate / deviceSampleRate)` per output sample with **linear interpolation**, apply gain, wrap into loop region when a loop is set and the head crosses `outSeconds`, stop (playing=false, hold position at end) at end of buffer. No locks, no allocation, no logging in this method. Mono sources duplicate to both channels; >2ch sources: take first two.
2. **`JuceAudioEngine : AudioEngine`** — owns one `BufferPlaybackSource` and implements the interface by writing the atomics. Device wiring lives in a separate `AudioDeviceHub` (one per app, not per deck): `juce::AudioDeviceManager` (stereo out, default device) + `juce::AudioSourcePlayer` + a small `juce::MixerAudioSource` that decks plug into. The hub is host-only functionality but must compile everywhere; guard nothing — JUCE handles Linux (ALSA) even if the container has no device (initialise may fail; app must keep running with a visible "no audio device" note rather than crash).

Unit tests render `BufferPlaybackSource` offline (call `prepareToPlay` + `getNextAudioBlock` into a scratch buffer) — no device needed; see `05-testing.md`.

## `state/` — StateManager (M4)

```cpp
enum class DeltaSource { local, remote };

class StateManager {
public:
    using Listener = std::function<void(const StateDelta& applied, const PlaybackState& newState, DeltaSource)>;
    const PlaybackState& getState(DeckId) const;
    void applyDelta(StateDelta delta, DeltaSource source);   // clamps via Ranges, merges, notifies
    int addListener(Listener);            // returns token
    void removeListener(int token);
};
```

Message-thread-only (`JUCE_ASSERT_MESSAGE_THREAD` in every method). `applyDelta` drops empty deltas, clamps every present field, merges into the deck's `PlaybackState`, then notifies listeners **after** state is consistent. Listeners registered (composition root wires them in `app/`): `EngineAdapter` (maps applied fields to `AudioEngine` calls on the right deck — only the fields present in the delta), UI components, and `SyncPublisher`. Special rule: an applied `playing:true` must carry position. Senders are expected to supply it themselves per protocol; if a local delta omits it, StateManager injects the deck's own last-stored `positionSeconds` before merging as a fallback — not a live engine read: `state/` has no dependency on `engine/`, so it cannot query the actual playhead. That stored value only tracks explicit seeks/loads, not the engine's continuous advance while playing (that's `PositionClock`'s job from M7), so a caller resuming playback after a pause must supply the true current position itself — read directly from its own `AudioEngine` reference (composition root only, e.g. `MainComponent`'s dev UI) — rather than relying on this fallback.

`state/PositionClock` (M7): while `playing`, UI shows extrapolated position (`lastSyncedPosition + elapsed × rate`); the *controller* additionally emits a `positionSeconds` resync delta every 5 s (a `juce::Timer` on the message thread that reads `engine.getCurrentPosition()`).

## `sync/` — SyncTransport (M7; NullTransport at M4)

```cpp
struct ConnectionInfo { juce::String url, roomCode, displayName; };

class SyncTransport {
public:
    virtual ~SyncTransport() = default;
    struct Callbacks {   // ALL invoked on the message thread (impl marshals)
        std::function<void(const juce::var& welcome)> onWelcome;
        std::function<void(const StateDelta&)> onRemoteDelta;
        std::function<void(const juce::var& msg)> onServerEvent;  // roleChanged/peerJoined/peerLeft/error/snapshot
        std::function<void(bool connected, juce::String reason)> onConnectionChange;
    };
    virtual void connect(const ConnectionInfo&, Callbacks) = 0;
    virtual void disconnect() = 0;
    virtual void sendDelta(const StateDelta&) = 0;
    virtual void sendClaimControl() = 0;
    virtual void sendReleaseControl() = 0;
    virtual void sendRequestSnapshot() = 0;
};
```

`NullTransport`: all no-ops, reports disconnected — used by the composition root until M7 and by tests. `WebSocketTransport`: IXWebSocket; builds/parses messages via `model/Serialization`; validates + clamps every incoming delta before surfacing it (network is untrusted, `06-security.md`); marshals every callback with `juce::MessageManager::callAsync` (capture by value; guard against callbacks arriving after `disconnect()` with a shared "alive" flag). Outgoing deltas from UI sliders are throttled to ≤ 30/s per control (trailing-edge coalescing) inside `SyncPublisher`, not in the transport.

`SyncPublisher`: a StateManager listener that forwards **only `DeltaSource::local`** deltas to the transport, and only while connected + role == controller (observers' local edits are blocked at the UI layer, this is the second line of defense).

## `ui/` — single deck first (M5), mixer at M8

- `DeckComponent`: track title, play/pause button, position slider (seek on release, display-only during play), gain slider (0–2), rate slider (0.5–2.0, center-detent at 1.0), loop in/out/clear buttons, time readout. All controls emit `StateDelta`s into StateManager (never call the engine directly). 30 Hz `juce::Timer` repaint for the playhead.
- `TrackListComponent`: repository tracks; double-click loads (emits `trackId` delta).
- `ConnectPanel` (M7): url (default `ws://127.0.0.1:8765`), room code, display name fields; connect/disconnect; status light; "Claim control" / "Release" button; peer list with roles. When role == observer, all deck controls are disabled (visually and functionally).
- `MixerComponent` (M8): crossfader A/B mapped to per-deck gain multipliers (equal-power curve), second `DeckComponent`.
- Keep UI dumb: render state, emit deltas. No business logic in components.

## `app/` — composition root (M1, grows)

`Main.cpp`: standard `juce::JUCEApplication` + `DocumentWindow` (see JUCE GuiApp example). `MainComponent` owns and wires: repository, engines (per deck), device hub, StateManager, adapters, transport, UI. All construction in one place; every dependency injected by constructor — no singletons, no globals.
