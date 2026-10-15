# Build environment matching the target: Debian Bookworm, arm64, GCC 12 (spec §5.3, §5.6).
# Cross-compiling from macOS is not supported; build in this container or on the CM4.
#
#   external_tools/docker/build.sh            # drm (target) build + tests
#   external_tools/docker/build.sh desktop    # desktop build inside the container
#   external_tools/docker/build.sh drm --tidy # with clang-tidy
#   external_tools/docker/build.sh shell      # interactive shell in the container
FROM --platform=linux/arm64 debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    # toolchain
    build-essential g++-12 cmake ninja-build git ca-certificates pkg-config \
    clang-format clang-tidy \
    # target display backend: GBM + EGL + KMS
    libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev \
    # desktop backend (never linked into the drm build)
    libsdl2-dev \
    # carplay dongle, touch input, audio
    libusb-1.0-0-dev libinput-dev libudev-dev libasound2-dev \
    # config/calibration (init-time only). Catch2 v3 comes via FetchContent;
    # the bookworm catch2 package is v2 and does not satisfy find_package(Catch2 3).
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-12 CXX=g++-12
WORKDIR /src
