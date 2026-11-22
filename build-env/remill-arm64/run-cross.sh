#!/usr/bin/env bash
# Cross-build remill for aarch64 on this x86_64 host: build.sh --cross in the opensa2-cross image
# (clang/lld 19), against the sniper arm64 sysroot (build-env/arm64/sysroot, from
# build-env/arm64/build_release.sh).
#   build-env/remill-arm64/run-cross.sh WORKDIR [--jobs N]
# WORKDIR holds downloads, sources, builds and the result (WORKDIR/out/remill-aarch64); about
# 6 GB. The upstream x86_64 package zip is taken from OpenSA2's private/lifters/ when present
# (else downloaded, 495 MB). SA2MR_SYSROOT names another sysroot.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:?usage: run-cross.sh WORKDIR [--jobs N]}"; shift
mkdir -p "$WORK/dl"
WORK="$(cd "$WORK" && pwd)"
ZIP="$ROOT/external/OpenSA2/private/lifters/remill_ubuntu-20.04_packages.zip"
ZIPMOUNT=()
[ -f "$ZIP" ] && ZIPMOUNT=(-v "$ZIP:/zip/remill_ubuntu-20.04_packages.zip:ro")
exec docker run --rm -i \
  -v "$HERE":/rb:ro \
  -v "$WORK":/w \
  -v "${SA2MR_SYSROOT:-$ROOT/build-env/arm64/sysroot}":/sysroot:ro \
  "${ZIPMOUNT[@]}" \
  -e SYSROOT=/sysroot \
  opensa2-cross bash -c "/rb/build.sh --cross --work /w $( [ -f "$ZIP" ] && echo --zip /zip/remill_ubuntu-20.04_packages.zip ) $*"
