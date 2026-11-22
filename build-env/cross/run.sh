#!/usr/bin/env bash
# Run a command in the x86_64 cross container (opensa2-cross): the OpenSA2 checkout at /work,
# the sniper arm64 sysroot at /usr/lib/gcc/aarch64-linux-gnu/sniper-sysroot (read only; under /usr/lib/gcc/ so the tree's header
# provenance check, host/d3d9gpu/test/extract_refs.py SYSTEM_ALLOWED, takes it for the system
# toolchain's), the arm64 host-deps prefix over
# /work/private/s5-tools, the x86 one at /x86-s5-tools (read only: its glslang is a build tool),
# and an arm64 tools/pins.json over the x86 one when build-env/arm64/pins.json exists. The GCC
# cross toolchain's own target glibc (/usr/aarch64-linux-gnu/{include,lib}, trixie's 2.41) is
# hidden behind empty mounts, so every header and library comes from the sniper sysroot. The Room
# Run generator (roomrun/) is mounted read only at /roomrun for host/xr/build.sh.
# Overrides (CI, .github/workflows/frame.yml, and tools/release/ci_app.sh use them):
#   SA2MR_OPENSA2     the OpenSA2 checkout (default external/OpenSA2)
#   SA2MR_ARM64_S5    the arm64 host-deps prefix (default build-env/arm64/s5-tools)
#   SA2MR_X86_S5      the x86 one, for glslang (default <OpenSA2>/private/s5-tools)
#   SA2MR_SYSROOT     the sniper arm64 sysroot (default build-env/arm64/sysroot)
#   SA2MR_ARM64_PINS  the arm64 pins overlay (default build-env/arm64/pins.json)
#   SA2MR_NO_GAME=1   no SA2 folder at /sa2 (CI never has the game; it is also left out when the
#                     Steam folder does not exist)
#   build-env/cross/run.sh '<cmd>'
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
A="$ROOT/build-env/arm64"
R="${SA2MR_OPENSA2:-$ROOT/external/OpenSA2}"
S5="${SA2MR_ARM64_S5:-$A/s5-tools}"
X86S5="${SA2MR_X86_S5:-$R/private/s5-tools}"
SYSROOT="${SA2MR_SYSROOT:-$A/sysroot}"
PINSF="${SA2MR_ARM64_PINS:-$A/pins.json}"
SA2="$HOME/.local/share/Steam/steamapps/common/Sonic Adventure 2"
PINS=()
[ -f "$PINSF" ] && PINS=(-v "$PINSF:/work/tools/pins.json:ro")
GAME=()
[ -z "${SA2MR_NO_GAME:-}" ] && [ -d "$SA2" ] && GAME=(-v "$SA2:/sa2:ro")
exec docker run --rm -i \
  -v "$R":/work \
  -v "$ROOT/roomrun":/roomrun:ro \
  -v "$S5":/work/private/s5-tools \
  -v "$X86S5":/x86-s5-tools:ro \
  -v "$SYSROOT":/usr/lib/gcc/aarch64-linux-gnu/sniper-sysroot:ro \
  --tmpfs /usr/aarch64-linux-gnu/include:ro --tmpfs /usr/aarch64-linux-gnu/lib:ro \
  "${PINS[@]}" "${GAME[@]}" \
  -w /work opensa2-cross bash -lc "$*"
