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
    src/state/StateManager.cpp
    src/state/PositionClock.cpp
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
# Cache-seeded before the subdirectory's own `option(USE_TLS ...)` runs, so that
# option() call becomes a no-op instead of overriding this.
set(USE_TLS OFF CACHE BOOL "Disable TLS: prototype is ws:// only" FORCE)

include(FetchContent)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG 2efe037c9cc96fd536774f17bdb5215161ee5087
)
FetchContent_MakeAvailable(ixwebsocket)

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
