find_package(Threads REQUIRED)
add_library(c3_app STATIC
    args.cpp
    config.cpp
    mock_command_sink.cpp
    toml.cpp
    settings_store.cpp
)
target_include_directories(c3_app PUBLIC ../)
target_link_libraries(c3_app PUBLIC Threads::Threads c3_log c3_telemetry c3_vehicle c3_warnings)
