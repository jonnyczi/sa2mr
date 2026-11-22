#!/usr/bin/env bash
# The Steam Frame build kit: the OpenSA2 fork's source (external/OpenSA2, `git archive HEAD`) as
# the on-device builder (platforms/steam-frame/sa2mr_build.py --kit) reads it, in a tarball whose
# top directory is kit/. It must hold sa2mr/ (the drivers, the hot order, translation_inputs.txt)
# and Documentation/sa2mr/function-map-trimmed.json, which are tracked in the fork's sa2mr branch.
#
#   build-env/frame-kit/make_kit.sh OUT.tar.gz [TOOLCHAIN_JSON]
#
# If any file under the OVERLAY paths below differs from HEAD or is not committed yet, the working
# file is put over the archive and a warning names it: commit them before a release.
# TOOLCHAIN_JSON, when given, goes in as kit/toolchain.json ({"url", "sha256"}: what step 1 of
# the builder fetches when the toolchain directory is empty).
#
# Then a deny scan, which refuses the kit (and deletes OUT) on any hit: a PE or ELF image, a
# .ll/.bc/.o/.a/.so/.obj/.exe/.dll file, a file over 20 MB, anything under private/, a symlink that
# points out of the kit. Nothing in the kit may come from the game: it is the public source only.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
R="$ROOT/external/OpenSA2"
OUT="${1:?usage: make_kit.sh OUT.tar.gz [TOOLCHAIN_JSON]}"
TCJ="${2:-}"
OVERLAY=(sa2mr Documentation/sa2mr)
MAX_BYTES=$((20 * 1024 * 1024))
REQUIRED=(sa2mr/frame/translation_inputs.txt Documentation/sa2mr/function-map-trimmed.json
          sa2mr/map/restore_map.py sa2mr/finalize_extras.py sa2mr/arm64/lift_arm64.py
          sa2mr/arm64/release_build_arm64.py sa2mr/arm64/hot/hot_order.txt tools/s4/rebuild_image.py)

case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/sa2mr-kit.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/kit"

head=$(git -C "$R" rev-parse HEAD)
branch=$(git -C "$R" rev-parse --abbrev-ref HEAD)
echo "kit: external/OpenSA2 $branch $head"
git -C "$R" archive --format=tar HEAD | tar -x -C "$STAGE/kit"

# Uncommitted files under the overlay paths: tracked ones that differ, and untracked ones that
# are not ignored (an ignored file, like __pycache__, is never taken).
overlaid=()
while IFS= read -r -d '' f; do
  [ -f "$R/$f" ] || continue
  if ! git -C "$R" cat-file -e "HEAD:$f" 2>/dev/null || ! git -C "$R" diff --quiet HEAD -- "$f"; then
    mkdir -p "$STAGE/kit/$(dirname "$f")"
    cp -p "$R/$f" "$STAGE/kit/$f"
    overlaid+=("$f")
  fi
done < <(git -C "$R" ls-files -z --cached --others --exclude-standard -- "${OVERLAY[@]}")
if [ "${#overlaid[@]}" -gt 0 ]; then
  echo "WARNING: ${#overlaid[@]} file(s) not committed at $head were taken from the working tree; commit them before a release:" >&2
  printf '  %s\n' "${overlaid[@]}" >&2
fi

[ -z "$TCJ" ] || cp "$TCJ" "$STAGE/kit/toolchain.json"
{
  echo "OpenSA2 $branch $head"
  [ "${#overlaid[@]}" -eq 0 ] || { echo "uncommitted, from the working tree:"; printf '  %s\n' "${overlaid[@]}"; }
} > "$STAGE/kit/KIT_SOURCE.txt"

missing=()
for f in "${REQUIRED[@]}"; do [ -f "$STAGE/kit/$f" ] || missing+=("$f"); done
[ "${#missing[@]}" -eq 0 ] || { echo "make_kit: the kit lacks ${missing[*]}" >&2; exit 1; }

# The deny scan.
deny=()
while IFS= read -r -d '' p; do
  rel="${p#"$STAGE/kit/"}"
  case "$rel" in private/*|*/private/*) deny+=("$rel: under private/"); continue ;; esac
  if [ -L "$p" ]; then
    t=$(readlink "$p")
    case "$t" in /*) deny+=("$rel: absolute symlink -> $t"); continue ;; esac
    real=$(realpath -m "$(dirname "$p")/$t")
    case "$real" in "$STAGE/kit"/*) ;; *) deny+=("$rel: symlink out of the kit -> $t") ;; esac
    continue
  fi
  [ -f "$p" ] || continue
  case "${rel,,}" in
    *.ll|*.bc|*.o|*.a|*.so|*.so.*|*.obj|*.exe|*.dll) deny+=("$rel: build output or executable suffix"); continue ;;
  esac
  size=$(stat -c %s "$p")
  [ "$size" -le "$MAX_BYTES" ] || { deny+=("$rel: $size bytes (over 20 MB)"); continue; }
  magic=$(head -c 4 "$p" | od -An -tx1 | tr -d ' \n')
  case "$magic" in
    7f454c46) deny+=("$rel: an ELF image"); continue ;;
    4d5a*)
      # MZ: a PE image when e_lfanew points at "PE\0\0".
      if [ "$size" -ge 64 ]; then
        off=$(od -An -tu4 -j 60 -N 4 "$p" | tr -d ' ')
        if [ "$off" -lt "$size" ] && [ "$(od -An -tx1 -j "$off" -N 4 "$p" | tr -d ' \n')" = 50450000 ]; then
          deny+=("$rel: a PE image")
        fi
      fi ;;
  esac
done < <(find "$STAGE/kit" \( -type f -o -type l \) -print0)
if [ "${#deny[@]}" -gt 0 ]; then
  echo "make_kit: the deny scan refused the kit:" >&2
  printf '  %s\n' "${deny[@]}" >&2
  rm -f "$OUT"
  exit 1
fi

files=$(find "$STAGE/kit" -type f | wc -l)
mkdir -p "$(dirname "$OUT")"
tar -C "$STAGE" --owner=0 --group=0 --numeric-owner --sort=name -czf "$OUT.tmp" kit
mv -f "$OUT.tmp" "$OUT"
echo "deny scan: $files files, no hit"
echo "KIT_OK $OUT $(stat -c %s "$OUT") bytes sha256 $(sha256sum "$OUT" | cut -d' ' -f1)"
