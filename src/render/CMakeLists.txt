set(SOURCES 
    alerts.cpp
    brand.cpp
    camera_surface.cpp
    dock.cpp
    fonts.cpp
    icons.cpp
    layout.cpp
    settings.cpp
    sheet.cpp
    status_tools.cpp
    tabs.cpp
    theme.cpp
    widgets.cpp
)
add_library(c3_render STATIC ${SOURCES})
target_include_directories(c3_render PUBLIC ../)
target_link_libraries(c3_render PUBLIC c3_warnings c3_vehicle c3_telemetry nanovg)
