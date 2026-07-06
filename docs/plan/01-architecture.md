# 01 — Architecture

What the app is: a real-time, shared-state DJ application. One user (the **controller**) manipulates playback; every connected user (**observers**) sees and hears the same state changes on their own machine, playing their own local copy of the audio. Audio never streams over the network — only state does.

## Core principles (binding)

1. **Non-destructive.** Source audio buffers are never modified.
2. **Optimistic updates.** The controller's local state and audio update immediately; the network send happens after, never before.
3. **Interchangeable layers.** Repository, engine, and transport are abstract interfaces. Implementations swap without touching other layers.
4. **Server is authoritative for *who controls* and *last-known state*;** clients are authoritative for their own audio rendering.
5. **Prototype-first.** One deck works before two decks exist. Sync comes after single-deck stability. Beat alignment is last.
6. **The network is untrusted.** Both server and client validate and clamp everything they receive (see `06-security.md`).

## Layer map

```
┌────────────────────────── client (C++ / JUCE 8) ──────────────────────────┐
│                                                                           │
│  UI (JUCE components)                                                     │
│    │ user actions                ▲ state change notifications             │
│    ▼                             │                                        │
│  StateManager ──────────────────────────────┐                             │
│    │ owns canonical local PlaybackState     │ notifies                    │
│    │ per deck; all changes flow through     ▼                             │
│    │ applyDelta()                    SyncPublisher ──► SyncTransport ──►──┼── WebSocket ──► server
│    ▼ parameter writes (atomics)      (only forwards         ▲            │
│  AudioEngine (per deck)               source==local)        │ incoming   │
│    ▲ buffers                                                │ deltas     │
│  AudioRepository (local files)        MessageThread marshal ┘            │
└───────────────────────────────────────────────────────────────────────────┘

┌──────────── server (Node.js + ws) ────────────┐
│ Room: clients, roles, canonical PlaybackState │
│ Validates → applies to canonical → broadcasts │
└───────────────────────────────────────────────┘
```

## The five contracts

Concrete C++ signatures live in `04-client.md`; this section defines responsibilities.

### 1. AudioRepository
Provides audio and metadata. The app never knows where audio comes from.
- `listAvailableTracks() → vector<TrackMetadata>`
- `getTrackMetadata(id) → optional<TrackMetadata>`
- `getAudioBuffer(id) → shared_ptr<const LoadedAudio>` (buffer + source sample rate)

Prototype implementation: local files under `client/assets/tracks/` described by a `manifest.json`. Production implementation (out of scope): object storage behind the same interface.

### 2. Data model (`TrackMetadata`, `PlaybackState`, `StateDelta`)
- `PlaybackState` is the serializable thing that syncs: `trackId, playing, positionSeconds, gain, playbackRate, pitchOffsetSemitones, loop`.
- `StateDelta` is a partial `PlaybackState` (every field optional) plus a `deck` id (`"A"` or `"B"`).
- Field names, types, and valid ranges are defined once, in `02-protocol.md` §Field reference. Model code, server validation, and fixtures all conform to that table.

### 3. AudioEngine
State in, sound out. Never owns state; never talks to the network or UI.
- `load(LoadedAudio)`, `play()`, `pause()`, `seek(seconds)`, `setGain(g)`, `setPlaybackRate(r)`, `getCurrentPosition()`.
- One engine instance per deck.
- Prototype implementation is JUCE-native (**not** Web Audio — that reference in `docs/architecture.md` is stale; see `docs/decisions.md`). Split into a pure, unit-testable DSP core (`BufferPlaybackSource`) and thin device wiring (`juce::AudioDeviceManager`) — see `04-client.md`.

### 4. StateManager
Holds canonical local state per deck. **Every** state change — local UI action or remote delta — flows through `applyDelta(delta, source)`. Subscribers (UI, engine adapter, sync publisher) are notified with the delta and its source (`local` | `remote`). The sync publisher forwards only `local` deltas to the transport; this prevents echo loops.

### 5. SyncTransport
Abstracts the network. `connect(url, hello)`, `disconnect()`, `sendDelta(delta)`, `sendControlRequest(...)`, plus callbacks for incoming messages and connection state. Implementations: `NullTransport` (pre-network milestones), `WebSocketTransport` (IXWebSocket). Transport callbacks arrive on a network thread and must be marshalled to the message thread before touching StateManager (see Threading below).

## Data flow

**Controller:** UI action → `StateManager.applyDelta(delta, local)` → subscribers fire: engine adapter pushes parameters to AudioEngine (immediately audible), UI re-renders, SyncPublisher → `transport.sendDelta`. The controller never waits for the server.

**Observer:** server broadcast → transport callback (network thread) → marshal to message thread → validate + clamp → `StateManager.applyDelta(delta, remote)` → engine + UI update. Remote deltas are **not** re-sent (source check).

**Join:** client connects, sends `hello`, receives `welcome` containing a full state snapshot, applies it as one remote delta per deck, then applies subsequent deltas.

**Position while playing** is *not* streamed. A delta carrying `playing:true` includes `positionSeconds` at that instant; every client advances position locally (`position += elapsed × playbackRate`). The controller sends a small position-resync delta every 5 s while playing; observers snap to it. Drift between resyncs is accepted for the prototype.

## Threading model (client) — binding rules

Three thread domains:

| Domain | Runs | Rules |
|---|---|---|
| **Message thread** (JUCE) | UI, StateManager, repository loads (async), transport marshalled callbacks | StateManager is message-thread-only; assert with `JUCE_ASSERT_MESSAGE_THREAD` in every public method |
| **Audio thread** (device callback) | `BufferPlaybackSource::getNextAudioBlock` | **No locks, no allocation, no logging, no JUCE message calls.** Reads parameters from `std::atomic` fields written by the message thread |
| **Network thread** (IXWebSocket internal) | Transport callbacks | Do nothing but parse-and-marshal: `juce::MessageManager::callAsync` into the message thread |

Parameter handoff message→audio thread: `std::atomic<float>` / `std::atomic<double>` per parameter, plus an atomic pointer swap (`std::shared_ptr` held by message thread, raw pointer or index published atomically) for buffer replacement on track load. Never hand a buffer to the audio thread while it could be freed; retire old buffers on the message thread only after the audio thread has published that it switched.

## Target directory layout (end state)

```
DJ-App/
├── client/
│   ├── CMakeLists.txt              # extends existing file: sources, tests, IXWebSocket
│   ├── assets/tracks/              # gitignored; manifest.example.json committed
│   ├── src/
│   │   ├── Main.cpp                # JUCEApplication subclass
│   │   ├── app/                    # MainWindow, MainComponent, wiring/composition root
│   │   ├── model/                  # TrackMetadata, PlaybackState, StateDelta, (de)serialization
│   │   ├── repository/             # AudioRepository.h, LocalFileRepository
│   │   ├── engine/                 # AudioEngine.h, JuceAudioEngine, BufferPlaybackSource
│   │   ├── state/                  # StateManager
│   │   ├── sync/                   # SyncTransport.h, NullTransport, WebSocketTransport, SyncPublisher
│   │   └── ui/                     # DeckComponent, TrackListComponent, ConnectPanel, MixerComponent
│   └── tests/                      # Catch2 tests, mirrors src/ structure
├── server/
│   ├── package.json  package-lock.json
│   ├── src/                        # index.js, server.js, room.js, validate.js, protocol.js, log.js
│   └── test/                       # node:test suites
├── shared/protocol/
│   ├── PROTOCOL-VERSION            # single line: 1
│   └── fixtures/                   # valid/invalid message JSON, consumed by both test suites
└── .github/workflows/ci.yml
```

Deck count: milestones M1–M7 implement deck `"A"` only, but every API, message, and state container is keyed by deck id from day one so M8 adds `"B"` without refactoring.
