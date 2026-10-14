find_package(Threads REQUIRED)
add_library(c3_app STATIC
    args.cpp
    background_io.cpp
    config.cpp
    dock_controller.cpp
    event_loop.cpp
    headless.cpp
    mock_command_sink.cpp
    render_loop.cpp
    sensor_source_factory.cpp
    toml.cpp
    settings_store.cpp
    telemetry_ingestor.cpp
    well_controller.cpp
)
target_include_directories(c3_app PUBLIC ../)
target_link_libraries(c3_app PUBLIC Threads::Threads c3_log c3_telemetry c3_vehicle c3_render c3_warnings)
