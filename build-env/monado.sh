#!/usr/bin/env bash
# Start Monado (nixpkgs) with no headset: the qwerty driver's simulated HMD and controllers, its
# compositor in a desktop window. Run it in one terminal, build-env/play-xr.sh in another.
#   build-env/monado.sh            a window (XCB under Xwayland)
#   MONADO_NULL=1 build-env/monado.sh   no window (null compositor, as xelee's tests run it)
# Qwerty keys (focus the Monado window): WASDQE move the HMD, right mouse drag turns it; hold LCtrl /
# LAlt for the left / right controller. See Monado's qwerty driver documentation.
# Prints the XR_RUNTIME_JSON the player needs (play-xr.sh reads it from this file's default too).
set -euo pipefail
MONADO=${MONADO:-$(nix build nixpkgs#monado --no-link --print-out-paths)}
export XR_RUNTIME_JSON="$MONADO/share/openxr/1/openxr_monado.json"
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json}"
export QWERTY_ENABLE=1 XRT_NO_STDIN=1
if [ "${MONADO_NULL:-0}" = 1 ]; then export XRT_COMPOSITOR_NULL=1; else export XRT_COMPOSITOR_FORCE_XCB=1; fi
echo "XR_RUNTIME_JSON=$XR_RUNTIME_JSON"
pkill -x monado-service 2>/dev/null || true
exec "$MONADO/bin/monado-service"
