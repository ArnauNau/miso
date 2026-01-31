cmake_minimum_required(VERSION 3.30)
project(miso C)

set(CMAKE_C_STANDARD 23)

# Default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Common warnings for all builds
set(COMMON_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# Debug configuration
set(DEBUG_FLAGS
    -g -O0
    -fsanitize=address,undefined
    -fsanitize-address-use-after-scope
)

# Release configuration
set(RELEASE_FLAGS
    -O2 -flto
)

# --- PGO toggles (only for miso) ---
option(MISO_PGO_GENERATE "Build instrumented binary to generate PGO profile" OFF)
set(MISO_PGO_PROFILE_DIR "" CACHE PATH "Directory where profraw files will be written")
set(MISO_PGO_USE_PROFDATA "" CACHE FILEPATH "Path to merged .profdata for PGO use phase")

# Add SDL3, SDL_ttf, SDL_image as subdirectories
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)

# Source files
set(MISO_SOURCES
    main.c
    profiler.c
    profiler.h
    game_clock.h
    camera/camera.c
    camera/camera.h
    ecs/entity.c
    ecs/entity.h
    ecs/sparse_set.c
    ecs/sparse_set.h
    ecs/ecs.c
    ecs/ecs.h
    renderer/renderer.c
    renderer/renderer.h
    renderer/renderer_internal.h
    renderer/ui.c
    renderer/ui.h
    renderer/nuklear_sdl3_gpu.h
    tilemap/tilemap.c
    tilemap/tilemap.h
    debug_ui.c
    debug_ui.h
    logger.h
)

# Define executable
add_executable(miso ${MISO_SOURCES})

target_compile_options(miso PRIVATE ${COMMON_WARNINGS})

# Apply flags based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(miso PRIVATE ${DEBUG_FLAGS})
    target_link_options(miso PRIVATE
            -fsanitize=address,undefined
            -fsanitize-address-use-after-scope
    )
    target_compile_definitions(miso PRIVATE MISO_DEBUG)
    message(STATUS "Debug build: sanitizers ON, optimization OFF, MISO_DEBUG defined")

elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(miso PRIVATE ${RELEASE_FLAGS})
    target_link_options(miso PRIVATE -flto)

    message(STATUS "Release build: sanitizers OFF, optimization ON, LTO ON")

endif()

# --- PGO Phase 1: Generate (instrument) ---
if(MISO_PGO_GENERATE)
    if(MISO_PGO_PROFILE_DIR STREQUAL "")
        message(FATAL_ERROR "MISO_PGO_GENERATE=ON requires MISO_PGO_PROFILE_DIR to be set")
    endif()

    message(STATUS "PGO Generation enabled. Instrumented binary will write profiles to: ${MISO_PGO_PROFILE_DIR}")
    # LLVM PGO instrumentation (Clang)
    target_compile_options(miso PRIVATE -fprofile-instr-generate)
    target_link_options(miso PRIVATE -fprofile-instr-generate)

    # Where runtime writes profraw
    # %m expands to binary name; creates one file per process.
    target_compile_definitions(miso PRIVATE
            "MISO_PGO_PROFILE_DIR=\"${MISO_PGO_PROFILE_DIR}\""
    )
endif()

# --- PGO Phase 2: Use (optimize with .profdata) ---
if(NOT MISO_PGO_USE_PROFDATA STREQUAL "")
    if(NOT EXISTS "${MISO_PGO_USE_PROFDATA}")
        message(FATAL_ERROR "MISO_PGO_USE_PROFDATA points to missing file: ${MISO_PGO_USE_PROFDATA}")
    endif()

    message(STATUS "PGO Use enabled. Using profile data from: ${MISO_PGO_USE_PROFDATA}")
    target_compile_options(miso PRIVATE -fprofile-instr-use=${MISO_PGO_USE_PROFDATA})
    target_link_options(miso PRIVATE -fprofile-instr-use=${MISO_PGO_USE_PROFDATA})
endif()

# Link against vendored libraries
target_link_libraries(miso PRIVATE
    SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

if(APPLE)
    set_target_properties(miso PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()
