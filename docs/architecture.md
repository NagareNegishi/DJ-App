# DJ App — Architecture Plan

## What this app is

A real-time, shared-state, audio manipulation tool. Multiple users see the same state. One user controls, all users observe changes instantly.

---

## Core Principles

- Non-destructive. Source audio buffers are never modified.
- Optimistic updates. The controlling user's local state updates immediately without waiting for server confirmation.
- Interchangeable layers. Audio engine, audio repository, and sync transport are each behind an interface. Implementations can be swapped without affecting other layers.
- Prototype-first. One track works before two tracks exist. Sync logic comes after both tracks are stable.

---

## Layers and Contracts

### 1. AudioRepository

Responsible for providing audio to the app. The app never cares where audio comes from.

```
AudioRepository (interface)
  getTrackMetadata(id) → TrackMetadata
  getAudioBuffer(id)   → AudioBuffer
  listAvailableTracks() → TrackMetadata[]
```

**Prototype implementation:** predefined local files.  
**Production implementation:** database or object storage behind the same interface.

---

### 2. AudioTrack (data model)

Represents one track's state. Does not own the audio buffer. Does not talk to the audio engine directly.

```
AudioTrack
  id
  metadata: TrackMetadata     ← duration, BPM, file reference
  state: PlaybackState        ← position, gain, playbackRate, pitchOffset, loopPoints
```

`PlaybackState` is the thing that gets synced over the network. It is serializable. The buffer is not.

---

### 3. AudioEngine

Wraps the platform audio layer. Takes state as input, produces sound as output. Never owns state itself.

```
AudioEngine (interface)
  load(buffer: AudioBuffer)
  play()
  pause()
  seek(seconds: number)
  setGain(value: number)
  setPlaybackRate(value: number)
  getCurrentPosition() → number
```

**Prototype implementation:** Web Audio API (browser).  
**Production implementation:** OS audio layer via a cross-platform library, behind the same interface.

The engine is a consumer of `PlaybackState`, not a source of truth.

---

### 4. StateManager

Holds the canonical local representation of track state. Applies incoming changes and notifies the UI and audio engine.

```
StateManager
  getState() → PlaybackState
  applyDelta(delta: StateDelta)
  subscribe(listener: (state) → void)
```

Both WebSocket events and local user actions flow through `applyDelta`. The UI and audio engine are both subscribers.

---

### 5. SyncTransport

Handles real-time communication with the server. Abstracts the network layer.

```
SyncTransport (interface)
  send(delta: StateDelta)
  onIncoming(handler: (delta: StateDelta) → void)
  connect()
  disconnect()
```

**Prototype and production implementation:** WebSocket.  
**Pattern:** Server holds canonical state. When a user changes a parameter, their client sends a delta. Server broadcasts that delta to all other connected clients.

---

## Data Flow

### Controlling user (User A)

```
User A interacts with UI
  ↓
StateManager.applyDelta(delta)        ← local state updates immediately
  ↓
AudioEngine.set*(newValue)            ← audio updates immediately
  ↓
SyncTransport.send(delta)             ← server receives delta
```

### Observing users (User B, C, D)

```
Server broadcasts delta
  ↓
SyncTransport.onIncoming fires
  ↓
StateManager.applyDelta(delta)
  ↓
UI re-renders + AudioEngine.set*(newValue)
```

User A never waits for the server before hearing their own change.

---

## Build Order

1. AudioRepository (local file implementation)
2. AudioEngine (Web Audio API implementation)
3. AudioTrack state model
4. StateManager
5. Single-track UI reacting to local state only
6. SyncTransport + server canonical state
7. Multi-user sync working on one track
8. Second track
9. Sync logic between tracks (beat alignment)

Beat alignment is intentionally last. It is a hard problem and depends on everything above it being stable.

---

## Decisions Deferred

- Conflict resolution when two users change the same parameter simultaneously (ignored for prototype)
- Timestretching / pitch shifting algorithm (requires real-time sample computation, not just parameter changes)
- Latency budget for production (browser Web Audio API is acceptable for prototype; sub-10ms requires OS-level audio for production)
