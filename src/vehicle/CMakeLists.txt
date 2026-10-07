set(SOURCES
    vehicle_commands.cpp
)
add_library(c3_vehicle STATIC ${SOURCES})
target_include_directories(c3_vehicle PUBLIC ../)
target_link_libraries(c3_vehicle PUBLIC c3_warnings)