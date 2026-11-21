#!/usr/bin/env bash
# Build + test inside the opensa2-build Docker image (host NixOS has no global toolchain).
# Usage: ./build.sh [extra command run inside the container after building]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
docker run --rm -u "$(id -u):$(id -g)" -v "$here":/w -w /w opensa2-build bash -c "
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build build &&
  ctest --test-dir build --output-on-failure ${1:+&& $1}"
