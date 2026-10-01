add_library(c3_log STATIC log.cpp boot_message.cpp)
target_include_directories(c3_log PUBLIC ../)
target_link_libraries(c3_log PUBLIC c3_warnings)