# DJ App — Stack Decisions

Companion to `dj-app-architecture.md`. This doc only covers stack and tooling. The architecture doc remains the source of truth for layers, contracts, and data flow.

---

## Decided Stack

### Client (the DJ app itself)

| Layer | Choice | Why |
|---|---|---|
| Language | C++ | Required by JUCE; standard for real-time audio |
| Audio + UI framework | JUCE 8 | Audio engine, file decoding, device I/O, UI, and basic networking in one library |
| Build system | CMake | JUCE 8's primary build system |
| Compiler (Windows host) | MSVC via Visual Studio Build Tools 2022 | Compiler only, no IDE |
| Compiler (dev container) | clang or gcc | Same source, Linux toolchain |
| Editor | VS Code | On both host and inside dev container |

### Sync server

| Layer | Choice | Why |
|---|---|---|
| Runtime | Node.js | Simplest WebSocket relay; server is a dumb broadcaster |
| Library | `ws` | Standard WebSocket library for Node |

Server holds canonical state and rebroadcasts deltas. No audio processing server-side. Language choice barely matters at this stage — Node is picked for fastest setup, not technical merit.

### Deferred libraries (interfaces only for now)

Per the architecture doc's interface-first principle, these are wrapped behind interfaces with passthrough/fake implementations until needed.

| Capability | Interface | Real implementation later | Trigger to integrate |
|---|---|---|---|
| Time-stretch / pitch-shift | `TimeStretcher` | SoundTouch or Rubber Band | When two tracks need to play at different rates |
| BPM / beat detection | `BeatDetector` | aubio | When beat alignment work begins |
| Low-latency audio driver | (JUCE handles transparently) | ASIO SDK | When latency budget tightens below 20ms |

---

## License Posture

- JUCE used under **AGPLv3** during prototype.
- Repository will be public on GitHub with an `AGPL-3.0` LICENSE file from day one.
- No paid JUCE license needed unless shipping closed-source binaries.
- Open question if going commercial later: $800 perpetual JUCE license + audit all GPL dependencies (Rubber Band, aubio, etc.) for commercial licensing or replacement with MIT/Apache alternatives.

---

## Dev Container Setup

### Purpose split

The dev container is for code editing, compilation, tests, and the sync server. Audio playback and real GUI testing happen on the Windows host.

```
Dev container (Linux):           Windows host:
  - VS Code remote                 - VS Code (same repo, no container)
  - Claude Code                    - MSVC compiler
  - clang/gcc                      - Build YourApp.exe
  - CMake build for Linux          - Run with real audio (WASAPI/ASIO)
  - Sync server                    - GUI testing
  - Unit tests
```

Same source tree, two build targets, two purposes.

### Base image

No official JUCE Docker image exists. Reference implementations:

- `dave-billin/juce-llvm-dev-container` — VS Code dev container, handles X11 + PulseAudio passthrough. Use as primary reference.
- `eyalamirmusic/JuceDevMachine` — Ubuntu + CMake + Clang + Ninja, multi-arch.

Start from one of these, adapt rather than copy blindly.

### Why not run audio in the container

- Audio passthrough from Docker to host adds latency and instability.
- Docker on Windows runs through WSL2, adding another virtualization layer.
- ASIO is unavailable on Linux entirely.
- Real-time audio + virtualization = unpredictable glitches.

Container builds the code. Host runs the audio app.

---

## Open Decisions

### 1. JUCE WebSocket vs external library

JUCE has basic WebSocket support. May or may not be sufficient for the sync layer.

| Option | Pro | Con |
|---|---|---|
| JUCE built-in | No extra dependency | Less feature-complete; less battle-tested |
| `websocketpp` or `Boost.Beast` | Mature, well-documented | Extra dependency, more build complexity |

**Decision trigger:** when implementing `SyncTransport`. Try JUCE first; switch if it falls short.

### 2. Time-stretch library when needed

| Option | License | Quality | Notes |
|---|---|---|---|
| SoundTouch | LGPL | Decent | Easiest to integrate, free for closed-source via LGPL |
| Rubber Band | GPL or commercial | Excellent | Industry standard; commercial license required for closed-source |
| Signalsmith Stretch | MIT | Good | Newer, fully permissive license |
| Élastique | Proprietary | Best | Expensive, used by Serato/Traktor; not realistic for indie |

**Decision trigger:** when single-track playback is stable and two-track work begins. Likely SoundTouch first for ease, Rubber Band if quality is insufficient.

### 3. Beat detection library when needed

| Option | License | Notes |
|---|---|---|
| aubio | GPL | De facto open-source choice, used by Mixxx |
| Essentia | AGPL or commercial | More accurate, heavier dependency |
| BTrack | GPL | Real-time focused |

**Decision trigger:** when beat alignment between two tracks is on the table. Default to aubio unless a specific reason emerges.

### 4. Audio format support beyond JUCE built-ins

JUCE handles WAV, AIFF, FLAC, MP3, Ogg Vorbis natively. If broader format support is needed (M4A, AAC, etc.):

| Option | Pro | Con |
|---|---|---|
| Stick with JUCE built-ins | Zero extra setup | Format coverage limited |
| libsndfile | Lightweight | No MP3/M4A |
| FFmpeg | Handles everything | Heavy dependency, LGPL or GPL depending on build |

**Decision trigger:** when a user-facing track import fails on a format they expect to work. Not before.

### 5. ASIO support

ASIO gives 5–10ms latency on Windows vs 20–40ms with WASAPI. Required for serious DJ use, not for prototype.

**Decision trigger:** when default Windows audio latency becomes a noticeable problem during real use. Requires downloading the ASIO SDK from Steinberg and accepting their license.

### 6. Sync server language for production

Node.js is the prototype choice. If production needs higher throughput or single-binary deployment:

| Option | Pro | Con |
|---|---|---|
| Node.js | Already chosen, fastest to build | GC pauses, JavaScript runtime overhead |
| Go | Single binary, great concurrency | Different language from client |
| Rust | Fastest, safest | Steeper learning curve, slower to build |

**Decision trigger:** when production scale or deployment constraints are real. Not in prototype.

---

## Things That Are Not Decisions

To be explicit about scope: these are intentionally out of scope for the stack doc.

- VST/AU plugin hosting — not in architecture
- MIDI controller mapping — later concern, JUCE supports it when needed
- Audio recording or streaming output — later concern
- Multi-channel routing beyond stereo — later concern
- Mobile or web ports — not on the roadmap

---

## Summary in One Paragraph

C++ with JUCE 8, built using CMake. MSVC on Windows host produces the `.exe`; clang/gcc inside a dev container handles compilation, tests, and the Node.js sync server. AGPLv3 license on the public repo means zero JUCE license cost during prototyping. Time-stretch, beat detection, and ASIO are deferred behind interfaces and only integrated when their respective triggers fire — not before single-track playback is solid.
