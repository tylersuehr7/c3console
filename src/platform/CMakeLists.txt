set(SOURCES file_watchdog.cpp)
add_library(c3_platform_common STATIC ${SOURCES})
target_include_directories(c3_platform_common PUBLIC ../)
target_link_libraries(c3_platform_common PUBLIC c3_warnings)

# DESKTOP backend builds with SDL2 + GL stack
if(C3_BACKEND STREQUAL "desktop")
    find_package(SDL2 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)

    add_library(c3_platform_desktop STATIC
        example_backend.cpp)

    target_include_directories(c3_platform_desktop PUBLIC src)
    target_link_libraries(c3_platform_desktop PUBLIC SDL2::SDL2 OpenGL::GL nanovg c3_warnings c3_log)

    # nvg_gl3.c instantiates third-party NanoVG code; silence its warnings only.
    # set_source_files_properties(src/platform/nvg_gl3.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

# DRM backend builds with KMS + GBM + EGL + GLES2 stack
if(C3_BACKEND STREQUAL "drm")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(C3_DRM REQUIRED IMPORTED_TARGET libdrm gbm egl glesv2 libinput libudev)

    add_library(c3_platform_drm STATIC
        example_drm.cpp)

    target_include_directories(c3_platform_drm PUBLIC src)
    target_link_libraries(c3_platform_drm PUBLIC PkgConfig::C3_DRM nanovg c3_warnings c3_log)

    # nvg_gles2.c instantiates third-party NanoVG code; silence its warnings only.
    # set_source_files_properties(src/platform/nvg_gles2.c PROPERTIES COMPILE_OPTIONS "-w")
endif()