#!/usr/bin/env bash
# Run a command in the aarch64 test bench (opensa2-arm64-test) under QEMU: the release at
# /opt/sa2mr (read only), the build directory at its build-time path (test binaries embed it), the SA2 Steam folder at /sa2
# (read only), and a persistent HOME (build-env/arm64/test/home) for the install and sessions.
#   build-env/arm64/test/run.sh '<cmd>'
# QEMU runs are capped to 8 cores (QEMU_CPUS, default 0-7) so the desktop stays responsive.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/external/OpenSA2/private/generated/s7/release"
mkdir -p "$HERE/home"
exec docker run --rm -i --platform linux/arm64 --cpuset-cpus "${QEMU_CPUS:-0-7}" \
  -v "$HERE/home":/home/dev \
  -v "$GEN/linux-aarch64":/opt/sa2mr:ro \
  -v "$GEN/build/linux-aarch64":/work/private/generated/s7/release/build/linux-aarch64 \
  -v "$HOME/.local/share/Steam/steamapps/common/Sonic Adventure 2":/sa2:ro \
  -e VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  opensa2-arm64-test bash -c "$*"
