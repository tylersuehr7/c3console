find_package(Threads REQUIRED)
add_library(c3_app STATIC
    example.cpp
)
target_include_directories(c3_app PUBLIC src)
target_link_libraries(c3_app PUBLIC Threads::Threads c3_log c3_telemetry c3_warnings)
