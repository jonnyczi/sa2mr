#!/usr/bin/env bash
# Run a command in the x86_64 cross container (opensa2-cross): the OpenSA2 checkout at /work,
# the sniper arm64 sysroot at /usr/lib/gcc/aarch64-linux-gnu/sniper-sysroot (read only; under /usr/lib/gcc/ so the tree's header
# provenance check, host/d3d9gpu/test/extract_refs.py SYSTEM_ALLOWED, takes it for the system
# toolchain's), the arm64 host-deps prefix over
# /work/private/s5-tools, the x86 one at /x86-s5-tools (read only: its glslang is a build tool),
# and an arm64 tools/pins.json over the x86 one when build-env/arm64/pins.json exists. The GCC
# cross toolchain's own target glibc (/usr/aarch64-linux-gnu/{include,lib}, trixie's 2.41) is
# hidden behind empty mounts, so every header and library comes from the sniper sysroot.
#   build-env/cross/run.sh '<cmd>'
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
A="$ROOT/build-env/arm64"
R="$ROOT/external/OpenSA2"
PINS=()
[ -f "$A/pins.json" ] && PINS=(-v "$A/pins.json:/work/tools/pins.json:ro")
exec docker run --rm -i \
  -v "$R":/work \
  -v "$A/s5-tools":/work/private/s5-tools \
  -v "$R/private/s5-tools":/x86-s5-tools:ro \
  -v "$A/sysroot":/usr/lib/gcc/aarch64-linux-gnu/sniper-sysroot:ro \
  --tmpfs /usr/aarch64-linux-gnu/include:ro --tmpfs /usr/aarch64-linux-gnu/lib:ro \
  "${PINS[@]}" \
  -v "$HOME/.local/share/Steam/steamapps/common/Sonic Adventure 2":/sa2:ro \
  -w /work opensa2-cross bash -lc "$*"
