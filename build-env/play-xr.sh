#!/usr/bin/env bash
# Play the VR (OpenXR) build of OpenSA2 on this NixOS machine (external/OpenSA2/host/xr).
#   build-env/play-xr.sh [sa2recomp args...]
# The VR release (private/generated/xr/release/linux-x86_64, made by host/xr/build.sh) runs like the
# flat one (build-env/play.sh), with SA2_XR=1 so the launcher starts bin/sa2_player_xr. Every SA2_XR*
# variable of this environment reaches the player (host/xr/README.md lists them). An OpenXR runtime
# must be up: XR_RUNTIME_JSON names it (build-env/monado.sh starts Monado and prints the line).
# Sound plays as in the flat build (the launcher passes --audio; player.err says "audio: SDL alsa
# driver"); only --no-sound turns it off.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REL="$ROOT/external/OpenSA2/private/generated/xr/release/linux-x86_64"
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json}"
export SA2_XR="${SA2_XR:-1}"
cd "$REL"
exec steam-run bin/sa2recomp "$@"
