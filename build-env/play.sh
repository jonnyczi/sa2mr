#!/usr/bin/env bash
# Play the locally built OpenSA2 (x86_64 Linux) on this NixOS machine.
#   build-env/play.sh            normal launch
#   build-env/play.sh --selftest scripted title-to-menu check
# steam-run supplies an FHS layout. VK_DRIVER_FILES pins one RADV ICD: steam-run otherwise
# exposes Mesa twice, and OpenSA2's shader translator refuses two identically named devices.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REL="$ROOT/external/OpenSA2/private/generated/s7/release/linux-x86_64"
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json}"
cd "$REL"
exec steam-run bin/sa2recomp "$@"
