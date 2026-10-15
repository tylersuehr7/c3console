#!/usr/bin/env bash
# Build and test c3console inside the Bookworm arm64 container (external_tools/docker/Dockerfile).
#
#   external_tools/docker/build.sh [desktop|drm] [--tidy] [--debug]
#   external_tools/docker/build.sh shell
#
# Output lands in build-<backend>-docker/ so it never collides with the host build/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="c3console-build:bookworm"
BACKEND="drm"
TIDY="OFF"
BUILD_TYPE="Release"
MODE="build"

for arg in "$@"; do
  case "$arg" in
    desktop|drm) BACKEND="$arg" ;;
    shell)       MODE="shell" ;;
    --tidy)      TIDY="ON" ;;
    --debug)     BUILD_TYPE="RelWithDebInfo" ;;
    -h|--help)   sed -n '2,7p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if ! docker info >/dev/null 2>&1; then
  echo "docker daemon is not running" >&2
  exit 1
fi

docker build --platform linux/arm64 -q -t "$IMAGE" "$ROOT/external_tools/docker" >/dev/null

RUN=(docker run --rm --platform linux/arm64
     -v "$ROOT":/src -w /src
     -u "$(id -u):$(id -g)" -e HOME=/tmp
     "$IMAGE")

if [[ "$MODE" == "shell" ]]; then
  exec "${RUN[@]/docker run/docker run -it}" bash
fi

BUILD_DIR="build-${BACKEND}-docker"
exec "${RUN[@]}" bash -euo pipefail -c "
  cmake -B '$BUILD_DIR' -G Ninja \
        -DC3_BACKEND='$BACKEND' \
        -DCMAKE_BUILD_TYPE='$BUILD_TYPE' \
        -DC3_ENABLE_CLANG_TIDY='$TIDY'
  cmake --build '$BUILD_DIR' -j
  ctest --test-dir '$BUILD_DIR' --output-on-failure
  echo
  echo '--- smoke run ---'
  './$BUILD_DIR/c3console' --source=mock --headless=1
"
