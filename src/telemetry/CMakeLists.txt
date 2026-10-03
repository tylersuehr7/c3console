add_library(c3_telemetry STATIC fault_codes.cpp)
target_include_directories(c3_telemetry PUBLIC ../)
target_link_libraries(c3_telemetry PUBLIC c3_warnings)