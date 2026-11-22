#!/usr/bin/env bash
# The cross builds' target sysroot: Valve's Steam Linux Runtime 3 "sniper" arm64 SDK (glibc 2.31,
# GCC 14 runtime), pinned by digest, extracted from its image without running it (docker export
# needs no QEMU). Used by build_release.sh, build-env/remill-arm64/run-cross.sh,
# build-env/frame-toolchain/build.sh and CI (.github/workflows/frame.yml).
#   build-env/arm64/make_sysroot.sh [DIR]      (default build-env/arm64/sysroot; kept when present)
# The SDK is open source (Steam Runtime: Debian packages); nothing in it is SEGA's.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64@sha256:966ad3ef70751b98253f031d22c5b75798ceb9c49612b0a3489fc5f8f58858fc"
[ "${1:-}" != --image ] || { echo "$SDK_IMAGE"; exit 0; }
DIR="${1:-$HERE/sysroot}"
if [ -d "$DIR/usr/lib/gcc-14" ]; then
  echo "sysroot: $DIR (present)"
  exit 0
fi
rm -rf "$DIR"; mkdir -p "$DIR"
cid=$(docker create --platform linux/arm64 "$SDK_IMAGE" /bin/true)
docker export "$cid" | tar -C "$DIR" -xf - usr lib bin etc
docker rm "$cid" >/dev/null
# Absolute symlinks (libfoo.so -> /lib/...) made relative, so they resolve inside the sysroot.
(cd "$DIR" && find usr lib -type l -lname '/*' | while read -r l; do
   ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" ".$(readlink "$l")")" "$l"; done)
# The arm64 prefix's pkg-config paths (/work/private/s5-tools/install) through the sysroot.
ln -sfn /work "$DIR/work"
echo "sysroot: $DIR from $SDK_IMAGE"
