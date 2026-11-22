#!/usr/bin/env bash
# Cross-build the iced-x86 wheel for aarch64 Linux from its sdist (PyPI has no aarch64 wheel).
# Runs inside the opensa2-cross image (build.sh starts it): Rust from toolchain.lock, the
# extension linked by clang-19 + lld against the sniper arm64 sysroot (glibc 2.31), the wheel
# assembled by mkwheel.py (cp38-abi3, so it loads on the Frame's Python 3.12).
#   build_iced_wheel.sh DL WORK SYSROOT OUTDIR [--update-lock]
# --update-lock writes a fresh Cargo.lock to OUTDIR instead of building with the pinned one.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/toolchain.lock"
DL="$1" W="$2" SYSROOT="$3" OUT="$4" UPDATE="${5:-}"
mkdir -p "$W" "$OUT"
sha() { awk -v n="$1" '$1==n {print $3}' <<< "$INPUTS"; }
file() { awk -v n="$1" '$1==n {print $2}' <<< "$INPUTS"; }

# ---- Rust ---------------------------------------------------------------------------------
RUST="$W/rust"
if [ ! -f "$RUST/.ok" ]; then
  rm -rf "$RUST" "$W/rust-unpack"; mkdir -p "$W/rust-unpack"
  for c in rustc cargo rust-std-x86 rust-std-a64; do
    tar -xf "$DL/$(file $c)" -C "$W/rust-unpack"
    d="$W/rust-unpack/$(file $c)"; d="${d%.tar.xz}"
    "$d/install.sh" --prefix="$RUST" --disable-ldconfig --without=rust-docs >/dev/null
  done
  rm -rf "$W/rust-unpack"
  touch "$RUST/.ok"
fi
export PATH="$RUST/bin:$PATH" CARGO_HOME="$W/cargo-home"
rustc --version

# ---- source -------------------------------------------------------------------------------
SRC="$W/iced-x86-$ICED_VERSION"
rm -rf "$SRC"
tar -xzf "$DL/$(file iced-x86)" -C "$W"
if [ "$UPDATE" = --update-lock ]; then
  (cd "$SRC" && cargo generate-lockfile)
  cp "$SRC/Cargo.lock" "$OUT/iced-x86-$ICED_VERSION.Cargo.lock"
  echo "wrote $OUT/iced-x86-$ICED_VERSION.Cargo.lock"; exit 0
fi
cp "$HERE/iced-x86-$ICED_VERSION.Cargo.lock" "$SRC/Cargo.lock"

# ---- build --------------------------------------------------------------------------------
LINK="$W/aarch64-cc"
printf '#!/bin/sh\nexec clang-19 --target=aarch64-linux-gnu --sysroot=%s -fuse-ld=lld -Wno-unused-command-line-argument "$@"\n' "$SYSROOT" > "$LINK"
chmod +x "$LINK"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="$LINK"
export CC_aarch64_unknown_linux_gnu="$LINK" AR_aarch64_unknown_linux_gnu=llvm-ar-19
export CARGO_PROFILE_RELEASE_STRIP=symbols CARGO_INCREMENTAL=0 SOURCE_DATE_EPOCH=0
# no build paths in the binary; the abi3 build needs no target Python (pyo3 abi3-py38)
export RUSTFLAGS="--remap-path-prefix=$CARGO_HOME=/cargo --remap-path-prefix=$SRC=/iced-x86-py --remap-path-prefix=$RUST=/rust"
export PYO3_CROSS=1
(cd "$SRC" && cargo fetch --locked --target aarch64-unknown-linux-gnu \
  && cargo build --release --locked --offline --target aarch64-unknown-linux-gnu)
SO="$SRC/target/aarch64-unknown-linux-gnu/release/lib_iced_x86_py.so"

# ---- licences of everything linked into the extension -------------------------------------
LIC="$OUT/licenses"
rm -rf "$LIC"; mkdir -p "$LIC/crates" "$LIC/rust"
cp "$SRC/LICENSE.txt" "$LIC/iced-x86-py.LICENSE.txt"
for f in COPYRIGHT LICENSE-APACHE LICENSE-MIT; do
  cp "$(find "$RUST/share/doc" -name "$f" -print -quit)" "$LIC/rust/$f" 2>/dev/null || true
done
# the crates built for this target (Cargo.lock also lists ones for other platforms)
(cd "$SRC" && cargo tree --offline --locked --target aarch64-unknown-linux-gnu -e normal,build --prefix none -f "{p}") \
  | awk '{print $1, substr($2, 2)}' | sort -u > "$W/used-crates.txt"
python3 - "$SRC/Cargo.lock" "$CARGO_HOME" "$LIC/crates" "$OUT/crates.txt" "$W/used-crates.txt" <<'EOF'
import pathlib, re, shutil, sys, tomllib
lock, home, dest, listing, used = sys.argv[1:]
want = {tuple(l.split()) for l in pathlib.Path(used).read_text().splitlines() if l.strip()}
pk = tomllib.loads(pathlib.Path(lock).read_text())["package"]
srcs = list(pathlib.Path(home, "registry", "src").glob("*"))
rows = []
for p in pk:
    if not p.get("source", "").startswith("registry+") or (p["name"], p["version"]) not in want:
        continue
    nv = f"{p['name']}-{p['version']}"
    d = next((s / nv for s in srcs if (s / nv).is_dir()), None)
    if d is None:
        sys.exit(f"crate source missing: {nv}")
    toml = tomllib.loads((d / "Cargo.toml").read_text())
    lic = toml.get("package", {}).get("license", "?")
    files = [f for f in sorted(d.iterdir()) if re.match(r"(?i)(licen[cs]e|copying|copyright|notice)", f.name) and f.is_file()]
    if not files:
        print(f"note: {nv} ({lic}) ships no licence file")
    (pathlib.Path(dest) / nv).mkdir(parents=True)
    for f in files:
        shutil.copy(f, pathlib.Path(dest) / nv / f.name)
    rows.append("\t".join([nv, lic, p["checksum"], f"https://crates.io/api/v1/crates/{p['name']}/{p['version']}/download"]))
pathlib.Path(listing).write_text("\n".join(rows) + "\n")
EOF

# ---- the wheel ----------------------------------------------------------------------------
# glibc: the highest GLIBC_x.y the extension needs (manylinux tag; must be <= MAX_GLIBC)
GL="$(llvm-readelf-19 -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)"
echo "extension needs glibc $GL; NEEDED: $(llvm-readelf-19 -d "$SO" | grep NEEDED | grep -o '\[.*\]' | tr '\n' ' ')"
[ "$(printf '%s\n%s\n' "$GL" "$MAX_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] || { echo "glibc $GL > $MAX_GLIBC" >&2; exit 1; }
MINOR="${GL#2.}"; [ "$MINOR" -ge 17 ] || MINOR=17
rm -f "$OUT"/iced_x86-*.whl
python3 "$HERE/mkwheel.py" "$SRC" "$SO" "2_$MINOR" "$OUT"
