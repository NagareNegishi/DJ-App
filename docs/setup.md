# DJ App — Setup Decisions

Companion to `dj-app-architecture.md` and `dj-app-stack.md`. Covers only project-setup decisions not in those docs.

---

## Repository

| Item | Decision |
|---|---|
| Name | `dj-app` |
| Visibility | Public on GitHub from day one |
| Layout | Monorepo |
| License | AGPL-3.0 (full text at repo root as `LICENSE`) |
| Per-file license headers | Skip for now |

README to include a note signaling openness to dual-licensing arrangements, keeping the door open for a future commercial pivot.

---

## Monorepo Layout

```
dj-app/
├── .devcontainer/
│   ├── devcontainer.json
│   ├── docker-compose.yml      ← only `app` service initially
│   └── Dockerfile              ← extends Microsoft C++ image
├── .gitignore
├── LICENSE                     ← AGPL-3.0 full text
├── README.md
├── client/                     ← C++/JUCE code
│   ├── CMakeLists.txt
│   └── src/
│       └── Main.cpp
├── server/                     ← placeholder
│   └── README.md
└── docs/
    ├── architecture.md
    ├── stack.md
    └── setup.md
```

`server/` stays empty until architecture build-order step 6. Directory shape is set early so later additions don't require restructuring.

---

## Dev Container

| Item | Decision |
|---|---|
| Base image | `mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04` |
| Customization | Custom `Dockerfile` extending the base, adding JUCE Linux build deps + `ninja-build` |
| Pattern | Mirror the existing `.NET` devcontainer (docker-compose, features, Claude Code volume, firewall, postCreateCommand) |
| Audio passthrough | None. Audio runs on Windows host only. |
| Compose services | `app` only initially. `sync-server` added at architecture build-order step 6. |
| In-container compiler | clang (gcc available as fallback via `build-essential`) |
| Build executor | Ninja (installed via `apt install ninja-build`) |

**JUCE Linux build dependencies — UNVERIFIED, verify against JUCE official docs before use.**

Suggested list (to be confirmed):

```
libasound2-dev
libjack-jackd2-dev
libcurl4-openssl-dev
libfreetype-dev
libfontconfig1-dev
libx11-dev
libxcomposite-dev
libxcursor-dev
libxext-dev
libxinerama-dev
libxrandr-dev
libxrender-dev
libwebkit2gtk-4.1-dev
libglu1-mesa-dev
mesa-common-dev
```

These are OS-level system libraries JUCE links against at compile time on Linux, not JUCE itself. Required in the container for compilation even though the GUI app runs on the Windows host. Exact list and package names need to be verified against JUCE's current Linux build documentation before writing the Dockerfile.

---

## Windows Host Toolchain

| Item | Decision |
|---|---|
| Compiler installer | Visual Studio Build Tools 2022 (no full IDE) |
| Workload | "Desktop development with C++" |
| Components included | `cl.exe`, Windows SDK, MSBuild, C++ runtime libraries, Ninja |
| Editor | VS Code |
| Build executor | Ninja (not MSBuild) |

Decision is reversible at zero cost: full Visual Studio Community can be installed alongside Build Tools later. Upgrade trigger: when real-time audio debugging becomes a serious time sink.

---

## JUCE Integration

| Item | Decision |
|---|---|
| Acquisition method | CMake `find_package` |
| Version pinning | Pinned tag cloned and installed in Dockerfile |
| Exact version | TBD at integration time — check `https://github.com/juce-framework/JUCE/releases` for current stable |
| Where JUCE lives | Installed to `/usr/local` in the container image; installed to system prefix by developer on Windows |
| Committed to repo | No |

`CMakeLists.txt` shape:

```cmake
find_package(JUCE CONFIG REQUIRED)
```

The initial plan used CMake `FetchContent` to clone JUCE at configure time, which had the appeal of handling acquisition automatically on both the container and the Windows host with no extra setup. It was rejected after verification: `FetchContent` does not appear in JUCE's official CMake documentation, and no reliable third-party source documents it as a supported path. An integration method that cannot be verified against official documentation cannot be reasoned about when it breaks.

### Container setup (Dockerfile — to be implemented)

Clone JUCE at the pinned tag, configure, build, install, then remove source and build dirs to keep the image lean:

```dockerfile
RUN git clone --depth=1 --branch <version> https://github.com/juce-framework/JUCE.git /tmp/JUCE \
    && cmake -S /tmp/JUCE -B /tmp/JUCE/build -G Ninja \
    && cmake --build /tmp/JUCE/build \
    && cmake --install /tmp/JUCE/build \
    && rm -rf /tmp/JUCE
```

---

## Build System

| Item | Decision |
|---|---|
| Configuration | CMake |
| Build executor (both platforms) | Ninja |
| Minimum CMake version | TBD at integration time — driven by JUCE's requirement |

---

## Session Goal

Scope of the next session: **target (a)** — repo created, `.devcontainer` builds, empty CMake project compiles inside the container.

Out of scope for the next session:
- JUCE GUI "Hello World" on Windows host (target b)
- Node.js sync server stub (target c)
- Any audio code
- Anything from the architecture build order beyond step 1

JUCE integration via `FetchContent` may be added if time permits, but priority is a green container build first.

---

## Open Items for Integration Time

- Exact JUCE version tag
- Exact CMake minimum version (driven by JUCE)
- `.gitignore` contents (standard C++/CMake/JUCE + `build/` + `.vs/`)
- README content
- Whether the Node.js devcontainer feature is added now or at sync-server time
- VS Code extensions beyond `ms-vscode.cpptools-extension-pack`
- CLA — only if/when outside contributors appear
