# Catch2 v3. Prefer a system install; fall back to fetching a pinned release.
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)

# One test binary. Add test_*.cpp files here as pure functions land:
# CRC-16/CCITT, protocol parsing, calibration interpolation, fault hysteresis,
# layout arithmetic, touch transform.
add_executable(c3console_tests
    test_main.cpp)

# c3_app and c3_render carry hvac and telemetry publicly; listing them again duplicates the
# archives on the link line.
target_link_libraries(c3console_tests PRIVATE c3_warnings Catch2::Catch2WithMain)

# Tests that read shipped files (config/calibration.json) locate them from the source tree,
# because ctest does not run them from the repository root.
target_compile_definitions(c3console_tests PRIVATE C3_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# openpty for the UartSource test: libutil on Linux, libc on macOS
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(c3console_tests PRIVATE util)
endif()

catch_discover_tests(c3console_tests)
