add_library(c3_telemetry STATIC fault_codes.cpp protocol.cpp csv_codec.cpp csv_recorder.cpp)
target_include_directories(c3_telemetry PUBLIC ../)
target_link_libraries(c3_telemetry PUBLIC c3_warnings)