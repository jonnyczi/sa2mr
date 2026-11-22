#!/usr/bin/env bash
# Build the Steam Frame translation toolchain: sa2mr-toolchain-aarch64.tar.zst, everything the
# Frame (SteamOS, aarch64, glibc 2.39, Python 3.12) needs besides its own git/clang/lld/curl/zstd
# to run OpenSA2's translation chain natively. All open source, built from pinned inputs; no SEGA
# code or data goes in.
#   build-env/frame-toolchain/build.sh OUTDIR [--jobs N] [--remill-work DIR] [--reuse-remill] [--no-verify]
# --reuse-remill takes the remill prefix already in the remill work dir (DIR/out/remill-aarch64, as
# CI's cache restores it) instead of running run-cross.sh. SA2MR_SYSROOT names the sysroot.
# OUTDIR gets:
#   sa2mr-toolchain-aarch64.tar.zst      the toolchain (top directory sa2mr-toolchain-aarch64/)
#   sa2mr-toolchain-aarch64.tar.zst.sha256
#   toolchain.lock                       every input: name, file, sha256, url (also in the tarball)
#   sa2mr-toolchain-aarch64.manifest     every file in the tarball with its sha256 and size
#   work/                                downloads and build trees (reused by the next run)
# Tarball layout:
#   remill/   SA2_REMILL_PREFIX (SA2_REMILL_MODE=unshare): bin/remill-lift-14,
#             share/remill/14/semantics/*.bc, include/remill/ (REMILL_INC, runtime compile input)
#   zig/      zig 0.15.2 aarch64-linux (SA2_ZIG=<toolchain>/zig/zig)
#   wheels/   iced-x86 (built here from the sdist) and pefile, for pip --no-index --find-links
#   licenses/ SOURCES.txt  toolchain.lock  BUILDINFO  env.sh  MANIFEST.sha256
# Needs docker (images opensa2-cross, and opensa2-arm64-test + QEMU binfmt for the check),
# the sniper arm64 sysroot in build-env/arm64/sysroot, and bash/tar/zstd/sha256sum/curl.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
. "$HERE/toolchain.lock"
. "$ROOT/build-env/remill-arm64/sources.lock"

OUT="${1:?usage: build.sh OUTDIR [--jobs N] [--remill-work DIR] [--no-verify]}"; shift
JOBS=16 RWORK="" VERIFY=1 REUSE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --jobs) JOBS="$2"; shift ;;
    --remill-work) RWORK="$2"; shift ;;
    --no-verify) VERIFY=0 ;;
    --reuse-remill) REUSE=1 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
  shift
done
mkdir -p "$OUT/work/dl"
OUT="$(cd "$OUT" && pwd)"
W="$OUT/work" DL="$OUT/work/dl"
RWORK="${RWORK:-$W/remill}"; mkdir -p "$RWORK"; RWORK="$(cd "$RWORK" && pwd)"
NAME=sa2mr-toolchain-aarch64
STAGE="$W/stage/$NAME"
SYSROOT="${SA2MR_SYSROOT:-$ROOT/build-env/arm64/sysroot}"
[ -d "$SYSROOT/usr/lib/aarch64-linux-gnu" ] || { echo "no sysroot at $SYSROOT (build-env/arm64/build_release.sh makes it)" >&2; exit 1; }
T0=$(date +%s)
step() { printf '\n== [%5ss] %s\n' "$(( $(date +%s) - T0 ))" "$*"; }
EPOCH=1700000000   # fixed mtime for the tarball entries

# ---- inputs -------------------------------------------------------------------------------
step "fetch pinned inputs into $DL"
fetch() {  # file sha url
  if [ ! -f "$DL/$1" ]; then curl -fL --retry 3 -o "$DL/$1.part" "$3" && mv "$DL/$1.part" "$DL/$1"; fi
  echo "$2  $DL/$1" | sha256sum -c --quiet - || { echo "sha256 mismatch: $1" >&2; exit 1; }
}
while read -r name file sha url; do [ -z "$name" ] || fetch "$file" "$sha" "$url"; done <<< "$INPUTS"
# upstream's x86_64 remill package: its semantics .bc (via build.sh) and its include/remill headers
ZIP="$ROOT/external/OpenSA2/private/lifters/$REMILL_ZIP_NAME"
[ -f "$ZIP" ] || { ZIP="$RWORK/dl/$REMILL_ZIP_NAME"; mkdir -p "$RWORK/dl"; }
# (run-cross.sh / build.sh look for it there too)
if [ ! -f "$ZIP" ]; then curl -fL --retry 3 -o "$ZIP.part" "$REMILL_ZIP_URL" && mv "$ZIP.part" "$ZIP"; fi
echo "$REMILL_ZIP_SHA256  $ZIP" | sha256sum -c --quiet - || { echo "sha256 mismatch: $ZIP" >&2; exit 1; }

# ---- remill (cross build, resumes through its .ok stamps) ---------------------------------
PREFIX="$RWORK/out/remill-aarch64"
if [ "$REUSE" = 1 ] && [ -x "$PREFIX/bin/remill-lift-14" ]; then
  step "remill: reusing $PREFIX (--reuse-remill)"
else
  step "remill: build-env/remill-arm64/run-cross.sh $RWORK"
  "$ROOT/build-env/remill-arm64/run-cross.sh" "$RWORK" --jobs "$JOBS" > "$W/remill.log" 2>&1 \
    || { tail -30 "$W/remill.log"; exit 1; }
  grep '^== ' "$W/remill.log" | tail -2
fi
[ -x "$PREFIX/bin/remill-lift-14" ] || { echo "no remill-lift-14 in $PREFIX" >&2; exit 1; }

# ---- iced-x86 wheel -----------------------------------------------------------------------
step "iced-x86 $ICED_VERSION wheel (cross, opensa2-cross)"
mkdir -p "$W/iced" "$W/iced-out"
docker run --rm -i -v "$HERE":/ft:ro -v "$DL":/dl:ro -v "$W/iced":/w -v "$W/iced-out":/o \
  -v "$SYSROOT":/sysroot:ro -e CARGO_BUILD_JOBS="$JOBS" opensa2-cross \
  bash /ft/build_iced_wheel.sh /dl /w /sysroot /o > "$W/iced.log" 2>&1 || { tail -40 "$W/iced.log"; exit 1; }
grep -E '^(rustc|extension needs)' "$W/iced.log"
WHEEL="$(ls "$W"/iced-out/iced_x86-*.whl)"

# ---- stage --------------------------------------------------------------------------------
step "stage $STAGE"
rm -rf "$W/stage"; mkdir -p "$STAGE"/{remill,wheels,licenses}
cp -a "$PREFIX"/. "$STAGE/remill/"
# (its BUILDINFO carries the assemble time; dropped so that rebuilds give the same tarball)
sed -i "/^date: /d" "$STAGE/remill/share/doc/remill-aarch64/BUILDINFO"
docker run --rm -i -v "$ZIP":/zip.zip:ro -v "$STAGE/remill":/o opensa2-cross python3 - "$REMILL_ZIP_TGZ" <<'EOF'
import io, os, sys, tarfile, zipfile
member = sys.argv[1]
with zipfile.ZipFile("/zip.zip") as z, tarfile.open(fileobj=io.BytesIO(z.read(member))) as t:
    n = 0
    for m in t.getmembers():
        i = m.name.find("/include/remill/")
        if m.isfile() and i >= 0:
            dest = "/o/include/remill/" + m.name[i + len("/include/remill/"):]
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            open(dest, "wb").write(t.extractfile(m).read()); n += 1
print(f"include/remill: {n} headers")
EOF
[ -f "$STAGE/remill/include/remill/Arch/X86/Runtime/State.h" ] || { echo "remill headers missing" >&2; exit 1; }
mkdir -p "$STAGE/zig"
tar -xf "$DL/$(awk '$1=="zig"{print $2}' <<< "$INPUTS")" -C "$STAGE/zig" --strip-components=1
cp "$WHEEL" "$STAGE/wheels/"
cp "$DL/$(awk '$1=="pefile"{print $2}' <<< "$INPUTS")" "$STAGE/wheels/"

# licences
L="$STAGE/licenses"
D="$PREFIX/share/doc/remill-aarch64"
cp "$D/LICENSE.remill" "$L/remill.LICENSE"; cp "$D/LICENSE.llvm" "$L/llvm.LICENSE.TXT"
cp "$D/LICENSE.xed" "$L/xed.LICENSE"; cp "$D/LICENSE.glog" "$L/glog.COPYING"
cp "$D/LICENSE.gflags" "$L/gflags.COPYING.txt"; cp "$D/LICENSE.sleigh" "$L/sleigh.LICENSE"
cp "$D/LICENSE.ghidra" "$L/ghidra.LICENSE"
cp "$STAGE/zig/LICENSE" "$L/zig.LICENSE"
mkdir -p "$L/iced-x86"; cp -r "$W/iced-out/licenses/." "$L/iced-x86/"
docker run --rm -i -v "$STAGE/wheels":/wh:ro opensa2-cross python3 -c "
import zipfile,sys; z=zipfile.ZipFile('/wh/pefile-$PEFILE_VERSION-py3-none-any.whl')
n=[m for m in z.namelist() if 'dist-info/' in m and 'LICENSE' in m.upper()][0]; sys.stdout.buffer.write(z.read(n))" > "$L/pefile.LICENSE"
[ -s "$L/pefile.LICENSE" ] || { echo "pefile licence missing" >&2; exit 1; }

# toolchain.lock: every input
{
  echo "# sa2mr-toolchain-aarch64 inputs: <name> <file> <sha256> <url>"
  echo "# frame toolchain (build-env/frame-toolchain/toolchain.lock)"
  sed '/^$/d' <<< "$INPUTS" | awk '{printf "%-14s %-50s %s %s\n", $1, $2, $3, $4}'
  echo "# remill build (build-env/remill-arm64/sources.lock)"
  sed '/^$/d' <<< "$SOURCES" | awk '{printf "%-14s %-50s %s %s\n", "remill-"$1, $2, $3, $4}'
  printf '%-14s %-50s %s %s\n' remill-pkg "$REMILL_ZIP_NAME" "$REMILL_ZIP_SHA256" "$REMILL_ZIP_URL"
  echo "# iced-x86 crates (build-env/frame-toolchain/iced-x86-$ICED_VERSION.Cargo.lock)"
  awk -F"\t" '{printf "%-14s %-50s %s %s\n", "crate", $1".crate", $3, $4}' "$W/iced-out/crates.txt"
} > "$STAGE/toolchain.lock"

# SOURCES.txt
{
  echo "Sources of everything in sa2mr-toolchain-aarch64 (exact versions; sha256s in toolchain.lock)."
  echo
  echo "remill/bin/remill-lift-14: remill 5.0.7, commit c0f90b9e4a251615288ed4b65ead81cea617da8c (Apache-2.0),"
  echo "  built for aarch64 by build-env/remill-arm64/build.sh with LLVM 14.0.3 (Apache-2.0 WITH LLVM-exception)"
  echo "  plus cxx-common v0.2.7's llvm-14 patches, XED v2022.04.17 + mbuild 09b6654b (Apache-2.0),"
  echo "  glog 0.5.0 and gflags 2.2.2 (BSD-3-Clause), sleigh 5ee2f2c1 + Ghidra 10.1.4 (Apache-2.0); static libstdc++/libgcc (GCC runtime exception)."
  echo "remill/share/remill/14/semantics, remill/include/remill: from upstream's release $REMILL_ZIP_URL"
  echo "  ($REMILL_ZIP_TGZ), Apache-2.0."
  sed '/^$/d' <<< "$SOURCES" | awk '{print "  "$1": "$4}'
  echo "zig/: zig 0.15.2 (MIT), $(awk '$1=="zig"{print $4}' <<< "$INPUTS")"
  echo "wheels/$(basename "$WHEEL"): iced-x86 $ICED_VERSION (MIT), built from $(awk '$1=="iced-x86"{print $4}' <<< "$INPUTS")"
  echo "  source: https://github.com/icedland/iced (src/rust/iced-x86-py); Rust 1.90.0 std (MIT OR Apache-2.0); crates:"
  awk -F"\t" '{print "    "$1" ("$2") "$4}' "$W/iced-out/crates.txt"
  echo "wheels/pefile-$PEFILE_VERSION-py3-none-any.whl: pefile $PEFILE_VERSION (MIT), $(awk '$1=="pefile"{print $4}' <<< "$INPUTS")"
  echo "  source: https://github.com/erocarrera/pefile"
} > "$STAGE/SOURCES.txt"

cat > "$STAGE/env.sh" <<'EOF'
# Source this to point OpenSA2's tools at the toolchain: . <toolchain>/env.sh
_SA2_TC="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export SA2_ZIG="$_SA2_TC/zig/zig"
export SA2_REMILL_PREFIX="$_SA2_TC/remill"
export SA2_REMILL_MODE="${SA2_REMILL_MODE:-unshare}"
export SA2_CROSS="${SA2_CROSS-}"
unset _SA2_TC
EOF

{
  echo "sa2mr-toolchain-aarch64, built by build-env/frame-toolchain/build.sh"
  echo "build host: $(uname -m)"
  echo "remill-lift-14 sha256: $(sha256sum "$STAGE/remill/bin/remill-lift-14" | cut -d' ' -f1)"
  echo "$(basename "$WHEEL") sha256: $(sha256sum "$WHEEL" | cut -d' ' -f1)"
  echo "build image opensa2-cross: $(docker image inspect -f '{{.Id}}' opensa2-cross)"
  echo "sniper arm64 sysroot: $("$ROOT/build-env/arm64/make_sysroot.sh" --image)"
} > "$STAGE/BUILDINFO"

( cd "$STAGE" && find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sed 's#  \./#  #' ) > "$STAGE/MANIFEST.sha256"

# ---- pack ---------------------------------------------------------------------------------
step "pack $OUT/$NAME.tar.zst"
TAR="$OUT/$NAME.tar.zst"
tar -C "$W/stage" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" \
  --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime --format=posix -cf - "$NAME" \
  | zstd -q -19 -T"$JOBS" -o "$TAR.part" -f
mv "$TAR.part" "$TAR"
( cd "$OUT" && sha256sum "$NAME.tar.zst" > "$NAME.tar.zst.sha256" )
cp "$STAGE/toolchain.lock" "$OUT/toolchain.lock"
( cd "$W/stage" && find "$NAME" -type f -print0 | LC_ALL=C sort -z | xargs -0 stat -c '%s %n' \
  | while read -r size path; do printf '%s  %10s  %s\n' "$(sha256sum "$path" | cut -d' ' -f1)" "$size" "$path"; done ) > "$OUT/$NAME.manifest"
echo "files: $(wc -l < "$OUT/$NAME.manifest")  unpacked: $(du -sh --apparent-size "$STAGE" | cut -f1)  tar.zst: $(du -h --apparent-size "$TAR" | cut -f1)"
cat "$OUT/$NAME.tar.zst.sha256"

# ---- check (aarch64 under QEMU) -----------------------------------------------------------
if [ "$VERIFY" = 1 ]; then
  step "verify under QEMU"
  "$HERE/verify.sh" "$TAR"
fi
step "TOOLCHAIN_BUILD_OK $TAR"
