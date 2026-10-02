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

add_subdirectory(src/logging)  # logging: its own platform code so can live without depending on app.
add_subdirectory(src/vehicle)  # vehicle: state/commands specific to C3; sits below telemetry and UI.

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(c3console src/main.cpp)
target_link_libraries(c3console PRIVATE c3_warnings c3_log c3_vehicle)
target_link_libraries(c3console PUBLIC nanovg)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(C3_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()