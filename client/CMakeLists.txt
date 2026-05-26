# Official reference:
# https://github.com/juce-framework/JUCE/blob/8.0.12/examples/CMake/GuiApp/CMakeLists.txt

# JUCE 8 requires CMake 3.22 minimum.
# dev container(Ubuntu 24.04) shipd with 3.28. Bump when the container CMake bumps.
cmake_minimum_required(VERSION 3.28)

project(DjAppClient
    VERSION 0.1.0
    LANGUAGES CXX
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
)

# Compile definitions consumed by JUCE headers.
# Disabling these avoids needing extra platform SDKs for the prototype.
# Flip to 1 (and add the corresponding NEEDS_* arg to juce_add_gui_app) when needed.
target_compile_definitions(dj-app-client PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
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
