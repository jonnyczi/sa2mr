#!/usr/bin/env bash
# The aarch64 (Steam Frame) release of OpenSA2, end to end, cross-compiled on this x86_64 host.
# Everything runs in Docker; QEMU is used only by the optional test step.
#
#   build-env/arm64/build_release.sh            images, sysroot, sources, lifted image, deps, release
#   build-env/arm64/build_release.sh test       + install and selftest under QEMU (slow)
#
# Output: external/OpenSA2/private/generated/s7/release/linux-aarch64/ and
#         external/OpenSA2/private/generated/s7/release/sa2mr-linux-aarch64.tar.gz
# Needs the x86_64 build done first (BUILD_NOTES.md): the lift, the tables and the x86 host deps
# (its glslang 16.6.0 is DXVK's build-time shader compiler here too).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
R="$ROOT/external/OpenSA2"
SDK_IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64@sha256:966ad3ef70751b98253f031d22c5b75798ceb9c49612b0a3489fc5f8f58858fc"
X="$ROOT/build-env/cross/run.sh"
step() { echo "=== $(date +%H:%M:%S) $*"; }

step "images"
docker image inspect opensa2-build >/dev/null || docker build -t opensa2-build "$ROOT/build-env"
docker build -q -t opensa2-cross "$ROOT/build-env/cross"

step "sniper arm64 SDK sysroot ($SDK_IMAGE)"
if [ ! -d "$HERE/sysroot/usr/lib/gcc-14" ]; then
  rm -rf "$HERE/sysroot"; mkdir -p "$HERE/sysroot"
  cid=$(docker create --platform linux/arm64 "$SDK_IMAGE" /bin/true)
  docker export "$cid" | tar -C "$HERE/sysroot" -xf - usr lib bin etc
  docker rm "$cid" >/dev/null
  # Absolute symlinks (libfoo.so -> /lib/...) made relative, so they resolve inside the sysroot.
  (cd "$HERE/sysroot" && find usr lib -type l -lname '/*' | while read -r l; do
     ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" ".$(readlink "$l")")" "$l"; done)
  # The arm64 prefix's pkg-config paths (/work/private/s5-tools/install) through the sysroot.
  ln -sfn /work "$HERE/sysroot/work"
fi

step "sources"
D="$HERE/s5-tools/dl"; mkdir -p "$D" "$HERE/s5-tools/src"
cp -n "$R/private/s5-tools/dl/SDL3-3.4.16.tar.gz" "$D/"
[ -f "$D/zstd-1.5.7.tar.gz" ] || curl -fsSL -o "$D/zstd-1.5.7.tar.gz" https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
[ -f "$D/wayland-1.18.0.tar.xz" ] || curl -fsSL -o "$D/wayland-1.18.0.tar.xz" https://wayland.freedesktop.org/releases/wayland-1.18.0.tar.xz
[ -f "$D/OpenXR-SDK-release-1.1.47.tar.gz" ] || curl -fsSL -o "$D/OpenXR-SDK-release-1.1.47.tar.gz" https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.47.tar.gz
( cd "$D" && sha256sum -c --quiet - ) <<'EOF'
7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68  SDL3-3.4.16.tar.gz
eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3  zstd-1.5.7.tar.gz
4675a79f091020817a98fd0484e7208c8762242266967f55a67776936c2e294d  wayland-1.18.0.tar.xz
82c7f4e3658578a22e438b5f005ecaf22c3f724b09fe031fa0f8ffc97c30c9ba  OpenXR-SDK-release-1.1.47.tar.gz
EOF
[ -d "$HERE/s5-tools/src/dxvk-3.1.1/.git" ] || cp -a "$R/private/s5-tools/src/dxvk-3.1.1" "$HERE/s5-tools/src/"

step "lifted image for aarch64-linux-musl, double f80 mode (24,040 modules)"
if [ ! -f "$R/private/generated/s8/pc-double/linux-aarch64-obj/summary.json" ]; then
  "$ROOT/build-env/run.sh" 'source tools/env.sh; .venv/bin/python private/sa2mr/arm64/lift_arm64.py --target aarch64-linux-musl --mode double --out private/generated/s8/pc-double/linux-aarch64-obj --jobs 8'
fi

step "host deps (SDL3, DXVK Native, dxbc-spirv, zstd, OpenXR loader) and the arm64 pins overlay"
"$X" 'bash private/sa2mr/arm64/build_deps.sh'
rm -f "$HERE/pins.json"
"$X" '.venv/bin/python private/sa2mr/arm64/arm64_pins.py --out private/sa2mr/arm64/pins.json'
mv "$R/private/sa2mr/arm64/pins.json" "$HERE/pins.json"

step "the release"
"$X" 'source tools/env.sh; .venv/bin/python private/sa2mr/arm64/release_build_arm64.py'

if [ "${1:-}" = test ]; then
  step "test bench (QEMU): install, then the selftest"
  docker build -q --platform linux/arm64 -t opensa2-arm64-test "$HERE/test"
  "$HERE/test/run.sh" 'cd /opt/sa2mr && bin/sa2-install --source folder /sa2 --yes'
  "$HERE/test/run.sh" 'cd /opt/sa2mr && xvfb-run -a -s "-screen 0 1280x720x24" bin/sa2recomp --selftest --timeout 7200 --out /home/dev/selftest'
fi
step "done"
