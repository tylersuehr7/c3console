cmake_minimum_required(VERSION 3.25)
project(c3console LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Toolchain and options
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(C3_BUILD_TESTS "Build the Catch2 test suite" ON)
option(C3_ENABLE_CLANG_TIDY "Run clang-tidy on every translation unit" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Which backend stack (DESKTOP or DRM) the binary builds with
set(C3_BACKEND "desktop" CACHE STRING "Display backend: desktop (SDL2 + GL) or drm (GBM + EGL + KMS)")
set_property(CACHE C3_BACKEND PROPERTY STRINGS desktop drm)
if(NOT C3_BACKEND MATCHES "^(desktop|drm)$")
    message(FATAL_ERROR "C3_BACKEND must be 'desktop' or 'drm' (got '${C3_BACKEND}')")
endif()

# Shared warning set for first-party code. Third-party targets do not use it.
add_library(c3_warnings INTERFACE)
target_compile_options(c3_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Woverloaded-virtual -Wcast-align -Wunused -Wnull-dereference
    -Werror=return-type
    # Designated initialisers that omit fields are the intended way to build Config in
    # tests; GCC 12 and clang 14 warn about them under -Wextra.
    -Wno-missing-field-initializers)

# ---------------------------------------------------------------------------
# Third party libraries
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# First party libraries
# ---------------------------------------------------------------------------

add_subdirectory(src/logging)    # platform logging; can live without depending on app.
add_subdirectory(src/vehicle)    # Pi -> MCU. State, command codec, acknowlege parser; no I/O.
add_subdirectory(src/telemetry)  # MCU -> Pi. Ingest framing, parsing, sources, calibration, I/O, etc.
add_subdirectory(src/platform)   # Desktop / DRM systems that target specific platform (test / screen / etc.)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(c3console src/main.cpp)
target_link_libraries(c3console PRIVATE c3_warnings c3_log c3_vehicle)
target_link_libraries(c3console PUBLIC nanovg)

if(C3_BACKEND STREQUAL "desktop")
    target_compile_definitions(c3console PRIVATE C3_BACKEND_DESKTOP=1)
    target_link_libraries(c3console PRIVATE c3_platform_desktop)
else()
    target_compile_definitions(c3console PRIVATE C3_BACKEND_DRM=1)
    target_link_libraries(c3console PRIVATE c3_platform_drm)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(C3_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()