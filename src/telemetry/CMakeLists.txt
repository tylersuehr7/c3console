set(SOURCES
    fault_codes.cpp
    protocol.cpp
    replay_source.cpp
    csv_codec.cpp
    csv_recorder.cpp
    calibration.cpp
    mock_source.cpp
)
add_library(c3_telemetry STATIC ${SOURCES})
target_include_directories(c3_telemetry PUBLIC ../)
target_link_libraries(c3_telemetry PUBLIC c3_warnings PRIVATE nlohmann_json::nlohmann_json)