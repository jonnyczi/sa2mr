#!/usr/bin/env bash
# Determinism check: lift a deterministic sample of real SA2 functions (N map functions with
# lift_all.py --to-next-entry's arguments + 5 jump-table arms with lift_arms.py's
# --entry_address arguments) with the pinned x86_64 remill (host, `unshare` bind-mount mode) and
# with an aarch64 remill prefix (QEMU, opensa2-arm64-test image, prefix mounted at /usr/local),
# then compare the raw .ll byte for byte.
#   build-env/remill-arm64/check/compare.sh PREFIX OUTDIR [N=200] [AARCH64_JOBS=8]
# OUTDIR gets game-derived data (jobs.json holds code bytes, *.ll is lifted game code): keep it
# in a scratch dir or under OpenSA2's private/, never in a repository.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R="$ROOT/external/OpenSA2"
PREFIX="$(cd "${1:?PREFIX}" && pwd)"; OUT="${2:?OUTDIR}"; N="${3:-200}"; AJ="${4:-8}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
SEC=(--security-opt seccomp=unconfined --security-opt apparmor=unconfined)
docker run --rm -i "${SEC[@]}" -v "$R":/work -v "$HERE":/chk:ro -v "$OUT":/cmp -w /work opensa2-build \
  bash -lc ".venv/bin/python /chk/mkjobs.py /cmp/jobs.json $N"
docker run --rm -i "${SEC[@]}" -v "$R/private/lifters/remill/usr/local":/remill:ro -v "$HERE":/chk:ro \
  -v "$OUT":/cmp opensa2-build bash -lc 'rm -rf /cmp/x86; python3 /chk/runjobs.py /cmp/jobs.json /cmp/x86 4 unshare -Urm --propagation unchanged sh -c "mount --bind /remill /usr/local && /usr/local/bin/remill-lift-14 {ARGS}"'
docker run --rm -i --platform linux/arm64 --cpuset-cpus "${QEMU_CPUS:-0-7}" -v "$PREFIX":/usr/local:ro \
  -v "$HERE":/chk:ro -v "$OUT":/cmp opensa2-arm64-test \
  bash -c "rm -rf /cmp/a64; python3 /chk/runjobs.py /cmp/jobs.json /cmp/a64 $AJ /usr/local/bin/remill-lift-14"
same=0; differ=0; missing=0
for f in "$OUT"/x86/*.ll; do
  g="$OUT/a64/$(basename "$f")"
  if [ ! -f "$g" ]; then missing=$((missing + 1)); echo "missing $(basename "$f")"
  elif cmp -s "$f" "$g"; then same=$((same + 1))
  else differ=$((differ + 1)); echo "differs $(basename "$f")"; fi
done
echo "REMILL_COMPARE identical=$same differ=$differ missing=$missing of $(ls "$OUT"/x86/*.ll | wc -l)"
[ "$differ" = 0 ] && [ "$missing" = 0 ]
