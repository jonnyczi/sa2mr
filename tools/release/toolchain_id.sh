#!/usr/bin/env bash
# The Steam Frame toolchain's id: the first 12 hex digits of a sha256 over every input that shapes
# sa2mr-toolchain-aarch64.tar.zst (the build scripts, the pinned sources, the build images, the
# sysroot pin). CI publishes the toolchain as the prerelease toolchain-<id>, builds it only when no
# release of that id exists, and the app's kit/toolchain.json names it.
#   tools/release/toolchain_id.sh          prints the id
#   tools/release/toolchain_id.sh --list   prints the inputs with their sha256s
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INPUTS=(
  build-env/Dockerfile
  build-env/cross/Dockerfile
  build-env/arm64/make_sysroot.sh
  build-env/frame-toolchain/build.sh
  build-env/frame-toolchain/build_iced_wheel.sh
  build-env/frame-toolchain/mkwheel.py
  build-env/frame-toolchain/toolchain.lock
  build-env/frame-toolchain/iced-x86-1.21.0.Cargo.lock
  build-env/remill-arm64/build.sh
  build-env/remill-arm64/run-cross.sh
  build-env/remill-arm64/sources.lock
  build-env/remill-arm64/aarch64-linux.cmake
  build-env/remill-arm64/stub-clang/ClangConfig.cmake
  build-env/remill-arm64/stub-clang/fake-clang
)
list=$(cd "$ROOT" && sha256sum "${INPUTS[@]}")
if [ "${1:-}" = --list ]; then echo "$list"; exit 0; fi
printf '%s\n' "$list" | sha256sum | cut -c1-12
