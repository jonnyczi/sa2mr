#!/usr/bin/env bash
# Check sa2mr-toolchain-aarch64.tar.zst on aarch64 under QEMU (no game data involved):
#   - python:3.12 arm64 (the Frame's Python): venv, pip install --no-index from wheels/, import
#     iced_x86 + pefile and decode a few synthetic bytes with iced;
#   - zig version, and zig cc building + running a C hello for aarch64-linux-musl;
#   - remill-lift-14 lifting a synthetic x86 byte string (push ebp; mov ebp,esp; pop ebp; ret),
#     first in SA2_REMILL_MODE=unshare's bind-mount form, then (qemu-user can't unshare) with the
#     prefix mounted at /usr/local; when OpenSA2's pinned x86_64 remill is present, the same
#     lift on the host must give byte-identical IR.
#   build-env/frame-toolchain/verify.sh TARBALL
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TAR="$(cd "$(dirname "${1:?TARBALL}")" && pwd)/$(basename "$1")"
NAME=sa2mr-toolchain-aarch64
V="$(dirname "$TAR")/work/verify"
rm -rf "$V"; mkdir -p "$V/ll"
tar -C "$V" --zstd -xf "$TAR" 2>/dev/null || zstd -dc "$TAR" | tar -C "$V" -xf -
TC="$V/$NAME"
( cd "$TC" && sha256sum -c --quiet MANIFEST.sha256 ) && echo "manifest: $(wc -l < "$TC/MANIFEST.sha256") files ok"
CPUS="${QEMU_CPUS:-0-7}"
PYIMG="${PYIMG:-python:3.12-slim-bookworm}"
BYTES=5589e55dc3
LIFT="--arch x86 --os windows --bytes $BYTES --address 0x401000 --entry_address 0x401000"

echo "== python wheels ($PYIMG, arm64)"
docker run --rm -i --platform linux/arm64 --cpuset-cpus "$CPUS" -v "$TC":/tc:ro "$PYIMG" bash -c '
  set -e; uname -m; python3 --version
  python3 -m venv /tmp/venv && /tmp/venv/bin/pip install -q --no-index --find-links /tc/wheels iced-x86 pefile
  /tmp/venv/bin/python - <<EOF
import iced_x86, pefile
d = iced_x86.Decoder(32, bytes.fromhex("5589e55dc3"), ip=0x401000)
f = iced_x86.Formatter(iced_x86.FormatterSyntax.INTEL)
print("iced", [f.format(i) for i in d], "pefile", pefile.__version__)
EOF
  /tmp/venv/bin/pip list 2>/dev/null | grep -Ei "iced|pefile"'

echo "== zig (opensa2-arm64-test)"
docker run --rm -i --platform linux/arm64 --cpuset-cpus "$CPUS" -v "$TC":/tc:ro opensa2-arm64-test bash -c '
  set -e; /tc/zig/zig version
  cd /tmp && printf "#include <stdio.h>\nint main(void){puts(\"zig cc ok\");return 0;}\n" > h.c
  ZIG_GLOBAL_CACHE_DIR=/tmp/zc ZIG_LOCAL_CACHE_DIR=/tmp/zc timeout 900 /tc/zig/zig cc -target aarch64-linux-musl -O2 h.c -o h && ./h'

echo "== remill-lift-14 (opensa2-arm64-test)"
docker run --rm -i --platform linux/arm64 --cpuset-cpus "$CPUS" -v "$TC/remill":/opt/remill:ro opensa2-arm64-test bash -c "
  unshare -Urm --propagation unchanged sh -c 'mount --bind /opt/remill /usr/local && /usr/local/bin/remill-lift-14 --version' >/dev/null 2>&1 \
    && echo 'unshare bind mount: works' || echo 'unshare bind mount: not available here (qemu-user); using a docker mount'" || true
docker run --rm -i --platform linux/arm64 --cpuset-cpus "$CPUS" -v "$TC/remill":/usr/local:ro -v "$V/ll":/ll opensa2-arm64-test \
  bash -c "/usr/local/bin/remill-lift-14 $LIFT --ir_out /ll/a64.ll && grep -c . /ll/a64.ll"
X86="$ROOT/external/OpenSA2/private/lifters/remill/usr/local"
if [ -x "$X86/bin/remill-lift-14" ]; then
  docker run --rm -i --security-opt seccomp=unconfined --security-opt apparmor=unconfined \
    -v "$X86":/remill:ro -v "$V/ll":/ll opensa2-build bash -c \
    "unshare -Urm --propagation unchanged sh -c 'mount --bind /remill /usr/local && /usr/local/bin/remill-lift-14 $LIFT --ir_out /ll/x86.ll'"
  cmp "$V/ll/x86.ll" "$V/ll/a64.ll" && echo "remill IR identical to the x86_64 package's ($(sha256sum < "$V/ll/a64.ll" | cut -c1-16))"
else
  echo "(no x86_64 remill package in OpenSA2's private/lifters: IR comparison skipped)"
fi
grep -q 'define .*sub_401000' "$V/ll/a64.ll" && echo "TOOLCHAIN_VERIFY_OK"
