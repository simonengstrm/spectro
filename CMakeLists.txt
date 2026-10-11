cmake_minimum_required(VERSION 3.24)

# Default to a Release-style build when no configuration was requested, so
# single-config generators still get optimizations. This has to happen before
# project(): Platform/Windows-MSVC.cmake sets CMAKE_BUILD_TYPE_INIT to Debug,
# which project() seeds into the cache, so a default chosen after this point
# would never apply on Windows. Multi-config generators ignore the variable.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build configuration")
endif()

project(PitchDetector LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Some bundled dependencies (glfw inside raylib) still declare a
# cmake_minimum_required below what CMake 4.x accepts on its own.
if(NOT CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# PortAudio (git submodule, built from source)
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/portaudio/CMakeLists.txt")
    message(FATAL_ERROR
        "extern/portaudio is empty. Fetch the submodule first:\n"
        "    git submodule update --init --recursive")
endif()

# The app calls PaUtil_* ring buffer functions, which PortAudio does not export
# from its Windows DLL, so link it statically everywhere.
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/portaudio EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# raylib (system package if present, otherwise fetched)
# ---------------------------------------------------------------------------
find_package(raylib 4.5 QUIET)
if(NOT raylib_FOUND)
    message(STATUS "raylib not found on the system - fetching it")
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
endif()

add_subdirectory(src)

target_link_libraries(PitchDetector PRIVATE
    portaudio
    raylib
)

# Warnings: the two compiler families take different flags.
if(MSVC)
    target_compile_options(PitchDetector PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(PitchDetector PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(PitchDetector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    # Keep the executable next to the binary dir root for every MSVC config.
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

if(WIN32)
    target_link_libraries(PitchDetector PRIVATE winmm)
endif()
