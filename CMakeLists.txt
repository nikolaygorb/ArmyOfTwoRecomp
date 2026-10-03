# armyoftworecomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(armyoftworecomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fix git symlinks on Windows (libmspack in SDK uses symlinks)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_symlinks.cmake)

# Apply local patches (thirdparty/patches) to the SDK submodule, idempotently.
# The submodule itself is never edited or committed.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_sdk_patches.cmake)

# temporary disabled
#include(cmake/build_optimizations.cmake)

# Build ReXGlue SDK from source (git submodule)
set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH "Path to rexglue-sdk source tree" FORCE)

# Enable both graphics backends
if(WIN32)
    set(REXGLUE_USE_D3D12 ON CACHE BOOL "Enable D3D12 graphics backend" FORCE)
endif()
set(REXGLUE_USE_VULKAN ON CACHE BOOL "Enable Vulkan graphics backend" FORCE)

# Enable SSSE3 support (required by ReXGlue SDK)
if(WIN32)
    add_compile_options(-mssse3)
endif()

include(generated/rexglue.cmake)

set(ARMYOFTWORECOMP_SOURCES
    src/main.cpp
    src/game_patches.cpp
    src/game_cvars.cpp
    src/mouse/mouse_look.cpp
    src/mouse/ao2_camera_hook.cpp
)

if(WIN32)
    list(APPEND ARMYOFTWORECOMP_SOURCES
        src/armyoftworecomp.rc
    )
endif()

if(WIN32)
    add_executable(armyoftworecomp WIN32 ${ARMYOFTWORECOMP_SOURCES})
else()
    add_executable(armyoftworecomp ${ARMYOFTWORECOMP_SOURCES})
endif()

rexglue_setup_target(armyoftworecomp GPU_PLUGINS xenos)

# Link against rexui for UI support (imgui, etc.)
target_link_libraries(armyoftworecomp PRIVATE rex::ui)

# SDL looks for this file (name set by settings/mapping.toml's
# hid_mappings_file) next to the exe at startup to fill in gamepads it
# doesn't already recognize. Sourced from
# https://github.com/mdqinc/SDL_GameControllerDB, see settings/README.md.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt")
    add_custom_command(TARGET armyoftworecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt"
                "$<TARGET_FILE_DIR:armyoftworecomp>/gamecontrollerdb.txt"
        COMMENT "Copying gamecontrollerdb.txt next to armyoftworecomp.exe")
endif()
