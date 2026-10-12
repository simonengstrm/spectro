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

# ---------------------------------------------------------------------------
# Dear ImGui + rlImGui, for the live controls panel
# ---------------------------------------------------------------------------
# The rlImGui tag matching raylib 5.5 uses Dear ImGui's 1.92+ texture API
# (ImTextureData / SetTexID), so the two versions have to move together.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7
    GIT_SHALLOW TRUE
)
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG Raylib_5_5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui rlimgui)

# Neither ships a CMakeLists, so they are compiled here as one library.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC raylib)

# ---------------------------------------------------------------------------
# Our own targets
# ---------------------------------------------------------------------------
# Warnings for our code only; the two compiler families take different flags.
add_library(spectro_warnings INTERFACE)
if(MSVC)
    target_compile_options(spectro_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(spectro_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Executables land in the binary dir root for every configuration, including
# under multi-config generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR})
endforeach()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
