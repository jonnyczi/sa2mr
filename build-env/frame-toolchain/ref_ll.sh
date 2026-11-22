#!/usr/bin/env bash
# The reference IR for verify.sh's remill check, from upstream's pinned x86_64 remill package
# (remill_ubuntu-20.04_packages.zip, sources.lock): it lifts the same synthetic bytes (push ebp;
# mov ebp,esp; pop ebp; ret; no game data) in the opensa2-build image, the package mounted at
# /usr/local. CI runs it on the x86_64 runner and compares on the aarch64 one (REF_LL=...).
#   build-env/frame-toolchain/ref_ll.sh ZIP OUT.ll
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
. "$ROOT/build-env/remill-arm64/sources.lock"
ZIP="$(cd "$(dirname "${1:?ZIP}")" && pwd)/$(basename "$1")"
OUT="${2:?OUT.ll}"
mkdir -p "$(dirname "$OUT")"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
echo "$REMILL_ZIP_SHA256  $ZIP" | sha256sum -c --quiet - || { echo "sha256 mismatch: $ZIP" >&2; exit 1; }
T="$(mktemp -d "$(dirname "$OUT")/ref-ll.XXXXXX")"
trap 'rm -rf "$T"' EXIT
LIFT="--arch x86 --os windows --bytes 5589e55dc3 --address 0x401000 --entry_address 0x401000"
docker run --rm -i -v "$ZIP":/zip.zip:ro -v "$T":/t opensa2-build python3 - "$REMILL_ZIP_TGZ" <<'EOF'
import io, sys, tarfile, zipfile
with zipfile.ZipFile("/zip.zip") as z, tarfile.open(fileobj=io.BytesIO(z.read(sys.argv[1]))) as t:
    t.extractall("/t/pkg", filter="tar")
EOF
docker run --rm -i -v "$T/pkg/usr/local":/usr/local:ro -v "$T":/t opensa2-build \
  bash -c "/usr/local/bin/remill-lift-14 $LIFT --ir_out /t/x86.ll"
cp "$T/x86.ll" "$OUT"
echo "REF_LL_OK $OUT $(sha256sum < "$OUT" | cut -c1-16)"
