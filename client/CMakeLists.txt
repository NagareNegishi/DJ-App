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

# Define the GUI app target.
# juce_add_gui_app wires up a normal CMake executable target plus JUCE-specific
# build steps (resource compilation, platform-specific bundling on macOS, etc.).
juce_add_gui_app(dj-app-client
    PRODUCT_NAME "DJ App"
    COMPANY_NAME "DJ App Project"
)

# `target_sources` adds source files to a target.
# Pass the target that needs the sources as the first argument,
# then a visibility parameter for the sources which should normally be PRIVATE.
# Finally, we supply a list of source files that will be built into the target.
target_sources(dj-app-client PRIVATE
    src/Main.cpp
    src/app/MainWindow.cpp
    src/app/MainComponent.cpp
)

# Direct module includes are preferred over JuceHeader.h
# juce_generate_juce_header(dj-app-client)

# Compile definitions consumed by JUCE headers.
# Disabling these avoids needing extra platform SDKs for the prototype.
# Flip to 1 (and add the corresponding NEEDS_* arg to juce_add_gui_app) when needed.
target_compile_definitions(dj-app-client PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:dj-app-client,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:dj-app-client,JUCE_VERSION>"
)

# Link only what's needed for a window. Audio modules come later.
# The PUBLIC interface targets apply JUCE's recommended compiler flags
# (warnings, optimisation hints) to our target.
target_link_libraries(dj-app-client
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Windows host may want to skip tests entirely (e.g. a quick GUI-only build).
option(DJAPP_BUILD_TESTS "Build unit tests" ON)

if(DJAPP_BUILD_TESTS)
    # Glob rather than list explicitly: tests/ mirrors src/ and grows every
    # milestone; CONFIGURE_DEPENDS re-runs the glob so new files are picked
    # up without touching this file again.
    file(GLOB DJAPP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

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

        # Mirror the app target's curl/webview opt-out: juce_core defaults to
        # JUCE_USE_CURL=1, which otherwise leaves undefined libcurl symbols at
        # link time since nothing here links libcurl.
        target_compile_definitions(dj-app-tests PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
        )

        # Only the JUCE modules the tested pure-logic code needs
        # (juce_data_structures brings in juce::var/JSON) — no GUI modules.
        target_link_libraries(dj-app-tests
            PRIVATE
                Catch2::Catch2WithMain
                juce::juce_core
                juce::juce_audio_basics
                juce::juce_data_structures
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
