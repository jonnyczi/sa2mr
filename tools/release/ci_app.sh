#!/usr/bin/env bash
# Build the Steam Frame app asset, sa2mr-app-linux-aarch64.tar.gz, from public sources only: no
# game, no executable, nothing translated. CI's app job runs it (.github/workflows/frame.yml); it
# also runs here against a fresh clone of the fork (SA2MR_OPENSA2=/path/to/clone).
#
#   tools/release/ci_app.sh OUTDIR [--toolchain-json FILE] [--jobs N]
#
# Steps (each in Docker; the images are built when missing):
#   1. images opensa2-build (with this user's uid/gid) and opensa2-cross; the sniper arm64 sysroot
#      (build-env/arm64/make_sysroot.sh)
#   2. the x86_64 glslang 16.6.0 (DXVK's build-time shader compiler; tools/pins.json's archive)
#   3. the arm64 host deps' sources (SDL3, zstd, wayland, OpenXR, the DXVK checkout at its pinned
#      commit), then sa2mr/arm64/build_deps.sh and the arm64 pins overlay (arm64_pins.py)
#   4. sa2mr/arm64/release_build_arm64.py --only players,install,launch,assemble,package --no-game
#      (the installer table is the tracked sa2mr/install/sa2_install_table.cpp)
#   5. the app tree: the release, kit/ (build-env/frame-kit/make_kit.sh, with toolchain.json),
#      frame/ (the builder, the Steam entry writer, the launch script, the VR game.env, install.sh),
#      sa2mr-vr.sh and share/game.env.vr; packed deterministically
#   6. tools/release/sa2mr_scan.py --profile app: SCAN_OK or the build fails
# Output in OUTDIR: sa2mr-app-linux-aarch64.tar.gz, .sha256, APP_SOURCE.txt; work/ (reused).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:?usage: ci_app.sh OUTDIR [--toolchain-json FILE] [--jobs N]}"; shift
TCJ="" JOBS="${JOBS:-$(nproc)}"
while [ $# -gt 0 ]; do
  case "$1" in
    --toolchain-json) TCJ="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift ;;
    --jobs) JOBS="$2"; shift ;;
    *) echo "ci_app: unknown argument $1" >&2; exit 2 ;;
  esac
  shift
done
mkdir -p "$OUT/work"
OUT="$(cd "$OUT" && pwd)"
W="$OUT/work"
export SA2MR_OPENSA2="${SA2MR_OPENSA2:-$ROOT/external/OpenSA2}"
R="$SA2MR_OPENSA2"
export SA2MR_ARM64_S5="$W/arm64-s5-tools" SA2MR_X86_S5="$W/x86-s5-tools"
export SA2MR_SYSROOT="${SA2MR_SYSROOT:-$ROOT/build-env/arm64/sysroot}"
export SA2MR_NO_GAME=1
X="$ROOT/build-env/cross/run.sh"
NAME=sa2mr-app-linux-aarch64
EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || echo 1700000000)}"
T0=$(date +%s)
step() { printf '\n== [%5ss] %s\n' "$(( $(date +%s) - T0 ))" "$*"; }
die() { echo "ci_app: $*" >&2; exit 1; }
py() { if command -v python3 >/dev/null; then python3 "$@"; else
         docker run --rm -i -v "$ROOT":"$ROOT" -v "$OUT":"$OUT" -v "$R":"$R":ro -w "$PWD" opensa2-build python3 "$@"; fi; }
pin() { py - "$R/tools/pins.json" "$1" "$2" <<'EOF'
import json, sys
print(json.load(open(sys.argv[1]))["tools"][sys.argv[2]][sys.argv[3]])
EOF
}

# ---- the tree holds no game -----------------------------------------------------------------
[ -f "$R/tools/pins.json" ] || die "no OpenSA2 checkout at $R"
if [ -d "$R/private" ]; then
  for d in "$R"/private/* "$R"/private/.[!.]*; do
    [ -e "$d" ] || continue
    case "$(basename "$d")" in
      generated) for g in "$d"/* ; do [ -e "$g" ] || continue
                   [ "$(basename "$g")" = s7 ] || die "$g exists: the app is built only from a tree without translated output"; done ;;
      s5-tools) [ -z "$(ls -A "$d")" ] || die "$d is not empty (the cross container's mount point only)" ;;
      *) die "$d exists: build the app from a fresh clone of the fork (no private/ but private/generated/s7)" ;;
    esac
  done
fi
mkdir -p "$R/private/generated/s7"

# ---- 1. images, sysroot ---------------------------------------------------------------------
step "images and the sniper arm64 sysroot"
docker image inspect opensa2-build >/dev/null 2>&1 || \
  docker build -q --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" -t opensa2-build "$ROOT/build-env"
docker image inspect opensa2-cross >/dev/null 2>&1 || docker build -q -t opensa2-cross "$ROOT/build-env/cross"
"$ROOT/build-env/arm64/make_sysroot.sh" "$SA2MR_SYSROOT"

# ---- 2. x86_64 glslang ----------------------------------------------------------------------
step "glslang (x86_64 build tool)"
GV=$(pin glslang version) GSHA=$(pin glslang sha256) GURL=$(pin glslang url)
mkdir -p "$SA2MR_X86_S5/dl"
if [ ! -x "$SA2MR_X86_S5/install/bin/glslang" ]; then
  G="$SA2MR_X86_S5/dl/glslang-$GV.tar.gz"
  [ -f "$G" ] || { curl -fsSL --retry 3 -o "$G.part" "$GURL" && mv "$G.part" "$G"; }
  echo "$GSHA  $G" | sha256sum -c --quiet - || die "glslang archive: sha256 differs from tools/pins.json"
  docker run --rm -i -v "$SA2MR_X86_S5":/g opensa2-build bash -c "set -e
    rm -rf /g/src /g/build; mkdir -p /g/src; tar -C /g/src -xzf /g/dl/glslang-$GV.tar.gz
    cmake -S /g/src/glslang-$GV -B /g/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/g/install \
      -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DGLSLANG_ENABLE_INSTALL=ON > /g/configure.log
    ninja -C /g/build -j$JOBS install > /g/build.log
    rm -rf /g/src /g/build"
fi
"$SA2MR_X86_S5/install/bin/glslang" --version 2>/dev/null | head -1 || \
  docker run --rm -v "$SA2MR_X86_S5":/g:ro opensa2-build /g/install/bin/glslang --version | head -1

# ---- 3. arm64 host deps ---------------------------------------------------------------------
step "arm64 host deps: sources"
D="$SA2MR_ARM64_S5/dl"; mkdir -p "$D" "$SA2MR_ARM64_S5/src"
fetch() { [ -f "$D/$1" ] || { curl -fsSL --retry 3 -o "$D/$1.part" "$2" && mv "$D/$1.part" "$D/$1"; }; }
fetch SDL3-3.4.16.tar.gz "$(pin sdl3 url)"
fetch zstd-1.5.7.tar.gz https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
fetch wayland-1.18.0.tar.xz https://wayland.freedesktop.org/releases/wayland-1.18.0.tar.xz
fetch OpenXR-SDK-release-1.1.47.tar.gz https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.47.tar.gz
( cd "$D" && sha256sum -c --quiet - ) <<EOF
$(pin sdl3 sha256)  SDL3-3.4.16.tar.gz
eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3  zstd-1.5.7.tar.gz
4675a79f091020817a98fd0484e7208c8762242266967f55a67776936c2e294d  wayland-1.18.0.tar.xz
82c7f4e3658578a22e438b5f005ecaf22c3f724b09fe031fa0f8ffc97c30c9ba  OpenXR-SDK-release-1.1.47.tar.gz
EOF
DX="$SA2MR_ARM64_S5/src/dxvk-$(pin dxvk version)" DXC=$(pin dxvk commit)
if [ "$(git -C "$DX" rev-parse HEAD 2>/dev/null)" != "$DXC" ]; then
  rm -rf "$DX" "$DX.part"
  git clone -q --no-recurse-submodules "$(pin dxvk url)" "$DX.part"
  git -C "$DX.part" -c advice.detachedHead=false checkout -q "$DXC"
  git -C "$DX.part" submodule --quiet update --init --recursive --depth 1
  mv "$DX.part" "$DX"
fi
echo "dxvk $(git -C "$DX" rev-parse HEAD) $(git -C "$DX" describe --tags 2>/dev/null || true)"

step "arm64 host deps: build (sa2mr/arm64/build_deps.sh)"
export SA2MR_ARM64_PINS="$W/no-pins-overlay.json"      # the tree's own pins while the deps build
rm -f "$SA2MR_ARM64_PINS"
"$X" "python3 -m venv .venv && .venv/bin/pip install -q --disable-pip-version-check -r requirements.txt"
"$X" "JOBS=$JOBS bash sa2mr/arm64/build_deps.sh" > "$W/build_deps.log" 2>&1 || { tail -40 "$W/build_deps.log"; die "build_deps.sh failed"; }
grep -E '^(== |sdl3 |dxvk )' "$W/build_deps.log" | tail -6
"$X" ".venv/bin/python sa2mr/arm64/arm64_pins.py --out private/generated/s7/arm64-pins.json"
mv "$R/private/generated/s7/arm64-pins.json" "$W/pins.json"
export SA2MR_ARM64_PINS="$W/pins.json"

# ---- 4. the release without the game --------------------------------------------------------
step "release_build_arm64.py --no-game"
rm -rf "$R/private/generated/s7/release"
"$X" "source tools/env.sh; .venv/bin/python sa2mr/arm64/release_build_arm64.py --only players,install,launch,assemble,package --no-game" \
  > "$W/release.log" 2>&1 || { tail -60 "$W/release.log"; die "release_build_arm64.py failed"; }
grep -q RELEASE_BUILD_ARM64_OK "$W/release.log" || die "no RELEASE_BUILD_ARM64_OK in $W/release.log"
grep -E '^(== |  bundled|  newest|RELEASE_BUILD_ARM64_OK)' "$W/release.log" | cut -c1-300
REL="$R/private/generated/s7/release/sa2mr-linux-aarch64.tar.gz"
[ -f "$REL" ] || die "no $REL"

# ---- 5. the app tree ------------------------------------------------------------------------
step "the app tree"
A="$W/app"
rm -rf "$A"; mkdir -p "$A"
tar -C "$A" -xzf "$REL"
[ -d "$A/sa2mr/bin" ] || die "the release tarball has no sa2mr/bin"
[ ! -e "$A/sa2mr/bin/sa2-game" ] || die "the release holds bin/sa2-game"
"$ROOT/build-env/frame-kit/make_kit.sh" "$W/kit.tar.gz" ${TCJ:+"$TCJ"}
tar -C "$A/sa2mr" -xzf "$W/kit.tar.gz"
F="$ROOT/platforms/steam-frame"
mkdir -p "$A/sa2mr/frame"
for f in sa2mr_build.py add_to_steam.py sa2mr-vr.sh game.env install.sh; do
  install -m "$( [ "${f##*.}" = env ] && echo 644 || echo 755 )" "$F/$f" "$A/sa2mr/frame/$f"
done
install -m 755 "$F/sa2mr-vr.sh" "$A/sa2mr/sa2mr-vr.sh"
install -m 644 "$F/game.env" "$A/sa2mr/share/game.env.vr"
{
  echo "sa2mr app (Steam Frame), built by tools/release/ci_app.sh"
  echo "root: $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)$(git -C "$ROOT" diff --quiet HEAD -- 2>/dev/null || echo ' (modified)')"
  echo "OpenSA2: $(git -C "$R" rev-parse HEAD)"
  [ -n "$TCJ" ] && echo "toolchain: $(tr -d '\n' < "$TCJ")" || echo "toolchain: none named (kit/toolchain.json missing)"
  echo "no game code or data: bin/sa2-game is translated and linked on the device from the user's own copy"
} > "$A/sa2mr/APP_SOURCE.txt"
cp "$A/sa2mr/APP_SOURCE.txt" "$OUT/APP_SOURCE.txt"

step "pack $NAME.tar.gz"
tar -C "$A" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" \
  --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime --format=posix -cf - sa2mr \
  | gzip -n -9 > "$OUT/$NAME.tar.gz.part"
mv "$OUT/$NAME.tar.gz.part" "$OUT/$NAME.tar.gz"
( cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" )
echo "files: $(find "$A/sa2mr" -type f | wc -l)  $(du -h --apparent-size "$OUT/$NAME.tar.gz" | cut -f1)  $(cut -c1-16 "$OUT/$NAME.tar.gz.sha256")..."

# ---- 6. the scan ----------------------------------------------------------------------------
step "sa2mr_scan.py --profile app"
py "$HERE/sa2mr_scan.py" --profile app --opensa2 "$R" "$OUT/$NAME.tar.gz" | tee "$W/scan.log"
grep -q '^SCAN_OK' "$W/scan.log" || { rm -f "$OUT/$NAME.tar.gz"; die "the scan refused the app (deleted)"; }
step "CI_APP_OK $OUT/$NAME.tar.gz"
