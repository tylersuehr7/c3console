# ---------------------------------------------------------------------------
# Third Party: NanoVG. The only rendering abstraction.
# ---------------------------------------------------------------------------
set(C3_NANOVG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nanovg")
if(NOT EXISTS "${C3_NANOVG_DIR}/src/nanovg.c")
    message(FATAL_ERROR "NanoVG submodule is missing.")
endif()
add_library(nanovg STATIC "${C3_NANOVG_DIR}/src/nanovg.c")
target_include_directories(nanovg SYSTEM PUBLIC "${C3_NANOVG_DIR}/src")
target_compile_options(nanovg PRIVATE -w)
set_target_properties(nanovg PROPERTIES C_STANDARD 99)

# ---------------------------------------------------------------------------
# Third Party: nlohomann/json. init-time only (config/calibration.json). Header-only.
# Prefer the distro package (bookworm: nlohmann-json3-dev); fall back to the pinned 
# release tarball, which is the 100 KB packaging drop, not the full repository.
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3.10 QUIET GLOBAL)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
    FetchContent_MakeAvailable(nlohmann_json)
endif()
