# Official reference:
# https://github.com/juce-framework/JUCE/blob/8.0.12/examples/CMake/GuiApp/CMakeLists.txt

# JUCE 8 requires CMake 3.22 minimum.
# dev container(Ubuntu 24.04) shipd with 3.28. Bump when the container CMake bumps.
cmake_minimum_required(VERSION 3.28)

# JUCE's own CMake support asserts a C compiler is enabled (JUCEModuleSupport.cmake),
# even though this project is C++-only; without it find_package(JUCE) fails to configure.
project(DjAppClient
    VERSION 0.1.0
    LANGUAGES CXX C
)

# C++20 matches what JUCE 8 itself uses internally, minimises cross-compiler differences.
# Bump this when a dependency or package starts requiring a newer standard.
set(CMAKE_CXX_STANDARD 20)

# Fail fast if the compiler cannot do C++20 instead of silently downgrading.
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No compiler-specific extensions (e.g. GNU extensions) so the same code
# compiles under MSVC on Windows host and clang/gcc in the container.
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE must be installed before configuring (see docs/setup.md).
find_package(JUCE CONFIG REQUIRED)

# Pure-logic core (model/, repository/, and future engine|state|sync/ non-GUI
# sources): compiled once here; dj-app-client and dj-app-tests both link it,
# so a new milestone's testable source is added in exactly one place.
#
# STATIC, not OBJECT: dj-app-client also links juce::juce_gui_basics directly,
# which (like dj-app-core's own juce_data_structures link) transitively pulls
# in juce_events — and JUCE's CMake support adds each such module's unity-build
# .cpp as a private source of *every* target that links it. An OBJECT library's
# objects are unconditionally force-linked into consumers, so dj-app-core's own
# private compile of juce_events.cpp collided with dj-app-client's own copy at
# link time ("multiple definition"). A real STATIC archive is pulled lazily —
# the linker only extracts a member to resolve an otherwise-unsatisfied symbol
# — so dj-app-client's own copy (linked first, directly) already satisfies
# those symbols and the archive's duplicate member is never extracted.
add_library(dj-app-core STATIC
    src/model/Serialization.cpp
    src/repository/LocalFileRepository.cpp
    src/engine/BufferPlaybackSource.cpp
    src/engine/JuceAudioEngine.cpp
    src/engine/EngineAdapter.cpp
    src/engine/IdentityTimeStretcher.cpp
    src/engine/SignalsmithTimeStretcher.cpp
    src/engine/QmDspBeatDetector.cpp
    src/state/StateManager.cpp
    src/state/CrossfaderState.cpp
    src/state/PositionClock.cpp
    src/state/DeckPositionClocks.cpp
    src/sync/NullTransport.cpp
    src/sync/SyncPublisher.cpp
)

target_include_directories(dj-app-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(dj-app-core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    # Shared wire-contract corpus (shared/protocol/fixtures), consumed by the
    # protocol fixture suite. Absolute at configure time so the test finds it
    # regardless of the working directory ctest happens to run it from.
    DJ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../shared/protocol/fixtures"
)

target_link_libraries(dj-app-core PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# IXWebSocket (BSD-3): the real WebSocket transport, client-only — never linked into
# dj-app-core, so model/state/tests stay free of it. Pinned by commit hash, not a
# movable tag name (06-security.md Supply-chain drift); this hash is what the v11.4.6
# tag resolves to. USE_TLS OFF: prototype is ws:// only (see WebSocketTransport.cpp).
# USE_ZLIB OFF: only enables the permessage-deflate compression extension, not needed
# for small state deltas, and avoids requiring a system zlib (present via apt on Linux,
# absent on a bare Windows Build Tools host). Both cache-seeded before the subdirectory's
# own option() calls run, so those calls become no-ops instead of overriding this.
set(USE_TLS OFF CACHE BOOL "Disable TLS: prototype is ws:// only" FORCE)
set(USE_ZLIB OFF CACHE BOOL "Disable zlib: avoid requiring it as a host dependency" FORCE)

include(FetchContent)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG 2efe037c9cc96fd536774f17bdb5215161ee5087
)
FetchContent_MakeAvailable(ixwebsocket)

# Signalsmith Stretch (MIT): the real time/pitch stretch engine behind
# SignalsmithTimeStretcher (10-beatsync-design.md). Header-only, so FetchContent +
# an include dir is all that's needed - no add_subdirectory, no library target to
# link. Pinned by commit hash on the repo's own tag (06-security.md Supply-chain
# drift), same pattern as IXWebSocket above. (Signalsmith Stretch v1.1.0 does not
# depend on Signalsmith Linear, despite an earlier assumption here that it did -
# see docs/plan/DEVIATIONS.md 2026-08-22 and its 2026-08-24 correction.)
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 44c8f865af9da8c29cc4a70a2d5a3ec83639c711 # v1.1.0
)
FetchContent_MakeAvailable(signalsmith_stretch)

# PRIVATE, not PUBLIC: SignalsmithTimeStretcher.cpp is the only translation unit
# that may include a Signalsmith header (it's a pimpl, see SignalsmithTimeStretcher.h) -
# same vendored-header containment as IXWebSocket via WebSocketTransport's pimpl.
# SYSTEM: these are vendored headers compiled under our own strict warning flags
# (juce_recommended_warning_flags) since they're #included directly rather than
# built as a separate target (unlike IXWebSocket above) - SYSTEM keeps their own
# internal warnings out of our build output without touching vendored source.
target_include_directories(dj-app-core SYSTEM PRIVATE
    ${signalsmith_stretch_SOURCE_DIR}
)

# qm-dsp (GPL-2.0-or-later): DetectionFunction + TempoTrackV2 beat detection,
# vendored for QmDspBeatDetector only (engine/QmDspBeatDetector.h/.cpp) - see
# docs/decisions.md Section 6.1 for the recorded GPL decision and
# docs/plan/10-beatsync-design.md ("BeatDetector") for the two-class analysis
# pipeline this wraps. qm-dsp ships no tagged release for its DSP core and no
# CMake target of its own (Makefile/MSVC-project only, same as Mixxx's own
# integration) - pinned to an exact commit SHA on master instead of a tag, a
# documented deviation from this project's tag-pinning convention
# (docs/plan/DEVIATIONS.md).
FetchContent_Declare(
    qmdsp
    GIT_REPOSITORY https://github.com/c4dm/qm-dsp.git
    GIT_TAG e34a3cc188332ed7c33cd9257ef164de5b587191
)
FetchContent_MakeAvailable(qmdsp)

# Minimal file set for DetectionFunction + TempoTrackV2 only, confirmed by
# reading the real #include chain (no FFTW/Vamp SDK/Boost - those are
# qm-dsp's own optional/test-only dependencies, not needed for this path):
# the two analysis classes, the PhaseVocoder + FFT wrapper DetectionFunction
# transitively needs, MathUtilities (both use it), and the bundled kissfft
# sources FFT.cpp calls into. base/Window.h, maths/MathAliases.h, and
# maths/nan-inf.h are template/header-only and need no .cpp. Its own static
# library, not folded directly into dj-app-core's sources, so the
# GPL-licensed object files stay clearly delineated from the rest of
# dj-app-core's (permissive) code.
add_library(qm-dsp-core STATIC
    ${qmdsp_SOURCE_DIR}/dsp/onsets/DetectionFunction.cpp
    ${qmdsp_SOURCE_DIR}/dsp/tempotracking/TempoTrackV2.cpp
    ${qmdsp_SOURCE_DIR}/dsp/phasevocoder/PhaseVocoder.cpp
    ${qmdsp_SOURCE_DIR}/dsp/transforms/FFT.cpp
    ${qmdsp_SOURCE_DIR}/maths/MathUtilities.cpp
    ${qmdsp_SOURCE_DIR}/ext/kissfft/kiss_fft.c
    ${qmdsp_SOURCE_DIR}/ext/kissfft/tools/kiss_fftr.c
)

# qm-dsp's own headers use repo-root-relative includes (e.g.
# "dsp/phasevocoder/PhaseVocoder.h"), so the fetched source root itself is
# the include path; kissfft's own .c files use unqualified includes
# ("kiss_fft.h"/"_kiss_fft_guts.h"), so its own directory (and the tools/
# subdirectory kiss_fftr.h lives in) also need to be on the path. PUBLIC on
# the source root only: QmDspBeatDetector.cpp (the sole consumer) needs it to
# resolve its own #includes; kissfft's paths stay PRIVATE since nothing
# outside FFT.cpp/PhaseVocoder.cpp ever needs to see kissfft directly.
target_include_directories(qm-dsp-core
    PUBLIC ${qmdsp_SOURCE_DIR}
    PRIVATE ${qmdsp_SOURCE_DIR}/ext/kissfft ${qmdsp_SOURCE_DIR}/ext/kissfft/tools
)

# qm-dsp's own build (build/general/Makefile.inc's KISSFFT_CFLAGS) always
# builds kissfft with scalar samples redefined to double instead of kissfft's
# own default (float) - FFT.cpp calls kiss_fftr()/kiss_fftri() with double*
# arrays throughout, so this isn't optional. PRIVATE: an internal kissfft
# build choice, invisible outside qm-dsp-core's own three kissfft-touching
# sources.
target_compile_definitions(qm-dsp-core PRIVATE kiss_fft_scalar=double)

# MSVC's <cmath> only defines M_PI when _USE_MATH_DEFINES is set before the
# include (GCC/Clang expose it unconditionally, which is why this only
# surfaces on the Windows host build) - MathUtilities.cpp uses M_PI directly.
if (MSVC)
    target_compile_definitions(qm-dsp-core PRIVATE _USE_MATH_DEFINES)
endif()

# Privately linked: dj-app-core's own consumers (dj-app-client, dj-app-tests)
# get qm-dsp-core's object code on their link line automatically (CMake's
# standard static-library private-dependency propagation) without inheriting
# its headers, keeping qm-dsp's own headers confined to
# QmDspBeatDetector.h/.cpp - the one file in dj-app-core that includes them.
target_link_libraries(dj-app-core PRIVATE qm-dsp-core)

# Define the GUI app target.
# juce_add_gui_app wires up a normal CMake executable target plus JUCE-specific
# build steps (resource compilation, platform-specific bundling on macOS, etc.).
juce_add_gui_app(dj-app-client
    PRODUCT_NAME "DJ App"
    COMPANY_NAME "DJ App Project"
    BUNDLE_ID "com.djappproject.dj-app-client"
)

# `target_sources` adds source files to a target.
# Pass the target that needs the sources as the first argument,
# then a visibility parameter for the sources which should normally be PRIVATE.
# Finally, we supply a list of source files that will be built into the target.
target_sources(dj-app-client PRIVATE
    src/Main.cpp
    src/app/MainWindow.cpp
    src/app/MainComponent.cpp
    src/ui/TrackListComponent.cpp
    src/ui/DeckComponent.cpp
    src/ui/MixerComponent.cpp
    src/ui/ConnectPanel.cpp
    src/engine/AudioDeviceHub.cpp
    src/sync/WebSocketTransport.cpp
)

# Direct module includes are preferred over JuceHeader.h
# juce_generate_juce_header(dj-app-client)

# Compile definitions consumed by JUCE headers.
# JUCE_APPLICATION_NAME_STRING/JUCE_APPLICATION_VERSION_STRING are app-specific;
# JUCE_WEB_BROWSER/JUCE_USE_CURL/CLIENT_SOURCE_DIR and the src/ include dir
# come from dj-app-core (linked below) as PUBLIC usage requirements.
target_compile_definitions(dj-app-client PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:dj-app-client,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:dj-app-client,JUCE_VERSION>"
)

# Link the shared pure-logic core, plus the GUI module it doesn't provide.
# The PUBLIC interface targets apply JUCE's recommended compiler flags
# (warnings, optimisation hints) to our target.
target_link_libraries(dj-app-client
    PRIVATE
        dj-app-core
        juce::juce_gui_basics
        juce::juce_audio_devices
        ixwebsocket
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Windows host may want to skip tests entirely (e.g. a quick GUI-only build).
option(DJAPP_BUILD_TESTS "Build unit tests" ON)

if(DJAPP_BUILD_TESTS)
    # Glob rather than list explicitly: tests/ mirrors src/ and grows every
    # milestone, now including subdirectories (e.g. tests/model/); CONFIGURE_DEPENDS
    # re-runs the glob so new files are picked up without touching this file again.
    file(GLOB_RECURSE DJAPP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    # Some checkouts (e.g. this implementer's worktree) intentionally have no
    # tests/ sources yet. Skip target creation silently rather than fetching
    # Catch2 and building an executable with nothing in it.
    if(DJAPP_TEST_SOURCES)
        include(FetchContent)

        # Pinned tag, not a floating branch: keeps the test build reproducible
        # and immune to upstream Catch2 changes landing mid-milestone.
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.8.1
        )
        FetchContent_MakeAvailable(Catch2)

        add_executable(dj-app-tests ${DJAPP_TEST_SOURCES})

        # model/ and repository/ pure-logic sources come from dj-app-core,
        # which also supplies the src/ include dir, JUCE_WEB_BROWSER/
        # JUCE_USE_CURL/CLIENT_SOURCE_DIR, and the JUCE modules that logic
        # needs (juce_data_structures for juce::var/JSON; juce_audio_formats
        # for AudioFormatManager/AudioFormatReader) as PUBLIC usage
        # requirements — no GUI modules.
        target_link_libraries(dj-app-tests
            PRIVATE
                dj-app-core
                Catch2::Catch2WithMain
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
        )

        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
        include(Catch)
        enable_testing()
        catch_discover_tests(dj-app-tests)
    endif()
endif()
