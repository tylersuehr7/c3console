# Host build of the MCU's pure logic, so the test suite can run the frame writer against the
# Pi's parser, the command parser against the Pi's command formatter, and pin the fault table.
# PlatformIO never reads this file; it compiles src/ on its own as an Arduino library.
add_library(c3_firmware STATIC
    src/c3fw/command.cpp
    src/c3fw/frame_writer.cpp)
target_include_directories(c3_firmware PUBLIC src)
target_link_libraries(c3_firmware PUBLIC c3_telemetry c3_vehicle c3_warnings)
