#!/usr/bin/env bash
# Build remill 5.0.7 (c0f90b9e) / LLVM 14.0.3 for aarch64 Linux, giving the same lifted IR as the
# pinned x86_64 package (remill_ubuntu-20.04_packages.zip). Runs inside a build container:
#   --cross   on x86_64, clang + lld against the sniper arm64 sysroot ($SYSROOT; run-cross.sh
#             runs it in the opensa2-cross image). LLVM's NATIVE tablegen is built for the host.
#   --native  on an aarch64 machine (CI: ubuntu-24.04-arm, in the sniper arm64 SDK container);
#             uses $CC/$CXX (default clang/clang++, else gcc/g++).
# Usage: build.sh --cross|--native --work DIR --zip PATH [--jobs N] [--out DIR]
#   --work  cache + build tree (downloads in DIR/dl, sources in DIR/src, builds in DIR/build)
#   --zip   upstream's remill_ubuntu-20.04_packages.zip (downloaded into DIR/dl when missing);
#           only its semantics bitcode is used, see sources.lock
#   --out   install prefix to assemble (default DIR/out/remill-aarch64): bin/remill-lift-14,
#           share/remill/14/semantics/*.bc, share/doc/remill-aarch64/ (licences, BUILDINFO)
# What matches the x86 package (cxx-common v0.2.7's llvm-14 port): LLVM 14.0.3 with the port's
# llvm/ patches, assertions ON, ABI-breaking checks OFF, RTTI+EH, targets
# AArch64;ARM;NVPTX;Sparc;WebAssembly;X86; XED v2022.04.17; glog 0.5.0; gflags 2.2.2; sleigh
# 5ee2f2c1 + Ghidra 10.1.4 with remill's sleigh patches; remill built Release with
# CMAKE_INSTALL_PREFIX=/usr/local, so it looks for its semantics in /usr/local/share/remill/14
# (the pipeline's `unshare` mode bind-mounts the prefix there). libstdc++/libgcc are linked
# statically, so the binary needs only glibc (>= the sysroot's 2.31).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=sources.lock
. "$HERE/sources.lock"

MODE="" WORK="" ZIP="" OUT="" JOBS="$(nproc)"
while [ $# -gt 0 ]; do
  case "$1" in
    --cross) MODE=cross ;;
    --native) MODE=native ;;
    --work) WORK="$2"; shift ;;
    --zip) ZIP="$2"; shift ;;
    --out) OUT="$2"; shift ;;
    --jobs) JOBS="$2"; shift ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
  shift
done
[ -n "$MODE" ] && [ -n "$WORK" ] || { sed -n '2,15p' "$0"; exit 2; }
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
DL="$WORK/dl" SRC="$WORK/src" B="$WORK/build" DEPS="$WORK/deps"
OUT="${OUT:-$WORK/out/remill-aarch64}"
ZIP="${ZIP:-$DL/$REMILL_ZIP_NAME}"
mkdir -p "$DL" "$SRC" "$B" "$DEPS"
export DEPS
T0=$(date +%s)
step() { printf '\n== [%5ss] %s\n' "$(( $(date +%s) - T0 ))" "$*"; }
done_() { [ -f "$B/.$1.ok" ]; }
mark() { touch "$B/.$1.ok"; }

# ---- toolchain -------------------------------------------------------------------------------
if [ "$MODE" = cross ]; then
  : "${SYSROOT:?--cross needs SYSROOT (the sniper arm64 sysroot)}"
  export CROSS_CC="${CROSS_CC:-clang-19}" CROSS_CXX="${CROSS_CXX:-clang++-19}"
  export CROSS_AR="${CROSS_AR:-$(command -v llvm-ar-19 || command -v llvm-ar)}"
  export CROSS_RANLIB="${CROSS_RANLIB:-$(command -v llvm-ranlib-19 || command -v llvm-ranlib)}"
  export SYSROOT
  CMAKE_TC=(-DCMAKE_TOOLCHAIN_FILE="$HERE/aarch64-linux.cmake")
  # plain compiler drivers for XED's mbuild, which has no toolchain-file notion
  mkdir -p "$B/bin"
  printf '#!/bin/sh\nexec %s --target=aarch64-linux-gnu --sysroot=%s -fuse-ld=lld -Wno-unused-command-line-argument "$@"\n' "$CROSS_CC" "$SYSROOT" > "$B/bin/acc"
  printf '#!/bin/sh\nexec %s --target=aarch64-linux-gnu --sysroot=%s -fuse-ld=lld -Wno-unused-command-line-argument "$@"\n' "$CROSS_CXX" "$SYSROOT" > "$B/bin/acxx"
  chmod +x "$B/bin/acc" "$B/bin/acxx"
  XCC="$B/bin/acc" XCXX="$B/bin/acxx" XAR="$CROSS_AR"
  HOST_CC="${HOST_CC:-$CROSS_CC}" HOST_CXX="${HOST_CXX:-$CROSS_CXX}"
  LLVM_CROSS=(-DLLVM_HOST_TRIPLE=aarch64-unknown-linux-gnu -DLLVM_TARGET_ARCH=AArch64
              "-DCROSS_TOOLCHAIN_FLAGS_NATIVE=-DCMAKE_C_COMPILER=$HOST_CC;-DCMAKE_CXX_COMPILER=$HOST_CXX;-DLLVM_INCLUDE_BENCHMARKS=OFF;-DLLVM_INCLUDE_TESTS=OFF;-DLLVM_INCLUDE_EXAMPLES=OFF;-DLLVM_INCLUDE_DOCS=OFF;-DLLVM_ENABLE_ZLIB=OFF;-DLLVM_ENABLE_TERMINFO=OFF;-DLLVM_ENABLE_LIBXML2=OFF;-DCMAKE_CXX_FLAGS=-include cstdint")
  # (the host tablegen sees the container's gcc 14 libstdc++, which no longer pulls in <cstdint>)
else
  [ "$(uname -m)" = aarch64 ] || { echo "--native must run on aarch64" >&2; exit 2; }
  CC="${CC:-$(command -v clang || command -v gcc)}" CXX="${CXX:-$(command -v clang++ || command -v g++)}"
  export CC CXX
  CMAKE_TC=(-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX")
  case "$CC" in *clang*) CMAKE_TC+=(-DCMAKE_EXE_LINKER_FLAGS_INIT=-fuse-ld=lld -DLLVM_USE_LINKER=lld) ;; esac
  XCC="$CC" XCXX="$CXX" XAR="$(command -v llvm-ar || command -v ar)"
  LLVM_CROSS=()
fi
CM=(cmake -G Ninja -DCMAKE_BUILD_TYPE=Release "${CMAKE_TC[@]}" -DCMAKE_PREFIX_PATH="$DEPS")

# ---- fetch + unpack --------------------------------------------------------------------------
step "fetch sources into $DL"
fetch() {  # path sha url
  if [ ! -f "$1" ]; then
    curl -fL --retry 3 -o "$1.part" "$3"
    mv "$1.part" "$1"
  fi
  echo "$2  $1" | sha256sum -c --quiet - || { echo "sha256 mismatch: $1" >&2; exit 1; }
}
while read -r name file sha url; do
  [ -n "$name" ] || continue
  fetch "$DL/$file" "$sha" "$url"
done <<< "$SOURCES"
fetch "$ZIP" "$REMILL_ZIP_SHA256" "$REMILL_ZIP_URL"

unpack() {  # name file -> $SRC/name (top dir stripped)
  [ -d "$SRC/$1" ] && return 0
  mkdir -p "$SRC/$1.tmp"
  if [ "$1" = llvm ]; then   # the monorepo: only llvm/ and the shared cmake/ are needed
    tar -xf "$DL/$2" -C "$SRC/$1.tmp" --strip-components=1 llvm-project-14.0.3.src/llvm llvm-project-14.0.3.src/cmake
  else
    tar -xf "$DL/$2" -C "$SRC/$1.tmp" --strip-components=1
  fi
  mv "$SRC/$1.tmp" "$SRC/$1"
}
if ! done_ unpack; then
  step "unpack + patch"
  rm -rf "$SRC"/*
  while read -r name file sha url; do
    [ -n "$name" ] || continue
    unpack "$name" "$file"
  done <<< "$SOURCES"
  # cxx-common v0.2.7's llvm-14 port patches that touch llvm/ (the others are clang/lld/flang/
  # bolt/compiler-rt install paths, projects not built here)
  for p in 0004-fix-dr-1734 0005-fix-tools-path 0010-fix-libffi 0020-fix-FindZ3.cmake \
           0021-fix-find_dependency 0024-remove-elf_relocation-checks; do
    patch -d "$SRC/llvm" -p1 --forward --batch < "$SRC/cxxcommon/ports/llvm-14/$p.patch"
  done
  # Ghidra: sleigh's pinned stable patch, then remill's two (FetchContent would `git am` them)
  for p in "$SRC/sleigh/patches/stable/0001-Small-improvements-to-C-decompiler-testing-from-CLI.patch" \
           "$SRC/remill/patches/sleigh/x86-ia.patch" "$SRC/remill/patches/sleigh/arm-thumb.patch"; do
    patch -d "$SRC/ghidra" -p1 --forward --batch < "$p"
  done
  mv "$SRC/mbuild" "$SRC/mbuild.tmp" && mkdir -p "$SRC/xedtree" && mv "$SRC/xed" "$SRC/xedtree/xed" \
    && mv "$SRC/mbuild.tmp" "$SRC/xedtree/mbuild"   # XED wants mbuild as a sibling directory
  mark unpack
fi

# ---- gflags, glog ----------------------------------------------------------------------------
if ! done_ gflags; then
  step "gflags 2.2.2"
  rm -rf "$B/gflags"
  "${CM[@]}" -S "$SRC/gflags" -B "$B/gflags" -DCMAKE_INSTALL_PREFIX="$DEPS" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTING=OFF -DGFLAGS_BUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DREGISTER_INSTALL_PREFIX=OFF >"$B/gflags.log"
  ninja -C "$B/gflags" -j "$JOBS" install >>"$B/gflags.log"
  mark gflags
fi
if ! done_ glog; then
  step "glog 0.5.0"
  rm -rf "$B/glog"
  "${CM[@]}" -S "$SRC/glog" -B "$B/glog" -DCMAKE_INSTALL_PREFIX="$DEPS" \
    -DBUILD_SHARED_LIBS=OFF -DWITH_GFLAGS=ON -DWITH_UNWIND=OFF -DWITH_GTEST=OFF -DBUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON >"$B/glog.log"
  ninja -C "$B/glog" -j "$JOBS" install >>"$B/glog.log"
  mark glog
fi

# ---- XED -------------------------------------------------------------------------------------
if ! done_ xed; then
  step "XED v2022.04.17"
  rm -rf "$B/xed" "$DEPS/include/xed" "$DEPS/lib/libxed"*
  ( cd "$SRC/xedtree/xed" && python3 mfile.py install --static --host-cpu=aarch64 \
      --cc="$XCC" --cxx="$XCXX" --ar="$XAR" --extra-ccflags="-O2 -fPIC" --extra-cxxflags="-O2 -fPIC" \
      --build-dir="$B/xed" --install-dir="$B/xed-kit" -j "$JOBS" ) >"$B/xed.log" 2>&1 \
    || { tail -40 "$B/xed.log"; exit 1; }
  mkdir -p "$DEPS/include" "$DEPS/lib" "$DEPS/share/xed"
  cp -r "$B/xed-kit/include/xed" "$DEPS/include/"
  cp "$B/xed-kit/lib/libxed.a" "$B/xed-kit/lib/libxed-ild.a" "$DEPS/lib/"
  cp "$SRC/cxxcommon/ports/xed/XEDConfig.cmake" "$DEPS/share/xed/"
  mark xed
fi

# ---- LLVM 14.0.3 -----------------------------------------------------------------------------
if ! done_ llvm; then
  step "LLVM 14.0.3 (libraries only; log $B/llvm.log)"
  "${CM[@]}" -S "$SRC/llvm/llvm" -B "$B/llvm" -DCMAKE_INSTALL_PREFIX="$DEPS" \
    "${LLVM_CROSS[@]}" \
    -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;NVPTX;Sparc;WebAssembly;X86" -DPACKAGE_VERSION=14.0.3 \
    -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ABI_BREAKING_CHECKS=FORCE_OFF \
    -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_THREADS=ON \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_LIBPFM=OFF \
    -DLLVM_INCLUDE_TOOLS=OFF -DLLVM_BUILD_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_UTILS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_INCLUDE_RUNTIMES=OFF -DLLVM_OPTIMIZED_TABLEGEN=ON -DLLVM_PARALLEL_LINK_JOBS=2 \
    -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations" >"$B/llvm.log" 2>&1 \
    || { tail -40 "$B/llvm.log"; exit 1; }
  ninja -C "$B/llvm" -j "$JOBS" install >>"$B/llvm.log" 2>&1 || { grep -m5 -B2 -A8 'error:' "$B/llvm.log"; tail -20 "$B/llvm.log"; exit 1; }
  mark llvm
fi

# ---- remill ----------------------------------------------------------------------------------
if ! done_ remill; then
  step "remill c0f90b9e: remill-lift-14 (log $B/remill.log)"
  "${CM[@]}" -S "$SRC/remill" -B "$B/remill" \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DLLVM_DIR="$DEPS/lib/cmake/llvm" -DClang_DIR="$HERE/stub-clang" -DXED_DIR="$DEPS/share/xed" \
    -Dglog_DIR="$DEPS/lib/cmake/glog" -Dgflags_DIR="$DEPS/lib/cmake/gflags" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_SLEIGH="$SRC/sleigh" -DFETCHCONTENT_SOURCE_DIR_GHIDRASOURCE="$SRC/ghidra" \
    -DREMILL_ENABLE_TESTING=OFF -DVCPKG_ROOT="" -DUSE_SYSTEM_DEPENDENCIES=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >"$B/remill.log" 2>&1 \
    || { tail -40 "$B/remill.log"; exit 1; }
  ninja -C "$B/remill" -j "$JOBS" remill-lift-14 >>"$B/remill.log" 2>&1 || { grep -m5 -B2 -A8 'error' "$B/remill.log"; tail -20 "$B/remill.log"; exit 1; }
  mark remill
fi

# ---- assemble the prefix ---------------------------------------------------------------------
step "assemble $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/share/remill/14/semantics" "$OUT/share/doc/remill-aarch64"
cp "$B/remill/bin/lift/remill-lift-14" "$OUT/bin/"
"${CROSS_STRIP:-$(command -v llvm-strip-19 || command -v llvm-strip || echo strip)}" "$OUT/bin/remill-lift-14"
python3 - "$ZIP" "$REMILL_ZIP_TGZ" "$OUT/share/remill/14/semantics" <<'EOF'
import io, sys, tarfile, zipfile
zpath, member, dest = sys.argv[1:]
with zipfile.ZipFile(zpath) as z, tarfile.open(fileobj=io.BytesIO(z.read(member))) as t:
    for m in t.getmembers():
        if m.isfile() and "/share/remill/14/semantics/" in m.name and m.name.endswith(".bc"):
            open(f"{dest}/{m.name.rsplit('/', 1)[1]}", "wb").write(t.extractfile(m).read())
EOF
( cd "$OUT/share/remill/14/semantics" && sha256sum -c --quiet - ) <<< "$(echo "$SEMANTICS" | sed '/^$/d; s/ /  /')"
D="$OUT/share/doc/remill-aarch64"
cp "$SRC/remill/LICENSE" "$D/LICENSE.remill"
cp "$SRC/llvm/llvm/LICENSE.TXT" "$D/LICENSE.llvm"
cp "$SRC/xedtree/xed/LICENSE" "$D/LICENSE.xed"
cp "$SRC/glog/COPYING" "$D/LICENSE.glog"
cp "$SRC/gflags/COPYING.txt" "$D/LICENSE.gflags"
cp "$SRC/sleigh/LICENSE" "$D/LICENSE.sleigh"
cp "$SRC/ghidra/LICENSE" "$D/LICENSE.ghidra"
{
  echo "remill 5.0.7 (c0f90b9e) for aarch64-linux, built by build-env/remill-arm64/build.sh --$MODE"
  echo "date: $(date -u +%FT%TZ)  host: $(uname -m)"
  echo "compiler: $("${CROSS_CXX:-$CXX}" --version | head -1)"
  echo "semantics: from $REMILL_ZIP_NAME ($REMILL_ZIP_SHA256), $REMILL_ZIP_TGZ"
  echo "sources:"; echo "$SOURCES" | sed '/^$/d' | awk '{print "  "$2" "$3" "$4}'
} > "$D/BUILDINFO"
step "done: $(du -sh "$OUT" | cut -f1) in $OUT"
