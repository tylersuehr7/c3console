set(SOURCES 
    log.cpp 
    boot_message.cpp
)
add_library(c3_log STATIC ${SOURCES})
target_include_directories(c3_log PUBLIC ../)
target_link_libraries(c3_log PUBLIC c3_warnings)