#!/usr/bin/env bash
# Frame-pacing summary of OpenSA2 sessions, from each session's pace.tsv.
#   build-env/pace-stats.sh            newest 6 sessions
#   build-env/pace-stats.sh N          newest N sessions
#   build-env/pace-stats.sh <session>  one session folder (name or path)
# Prints: kind (flat/xr), frames, mean fps, frames >25 ms, frames whose game
# frame was ready >5 ms after due (the game ran behind), and fps per 5 s window.
set -euo pipefail
SESS="${XDG_DATA_HOME:-$HOME/.local/share}/sa2recomp/sessions"
if [[ $# -eq 1 && ! "$1" =~ ^[0-9]+$ ]]; then
  list=("$( [[ -d "$1" ]] && echo "$1" || echo "$SESS/$1" )")
else
  mapfile -t list < <(ls -td "$SESS"/*/ | head -n "${1:-6}")
fi
for s in "${list[@]}"; do
  s="${s%/}"
  [[ -f "$s/pace.tsv" ]] || { echo "$(basename "$s") no pace.tsv"; continue; }
  kind=flat; head -c 400 "$s/player.err" 2>/dev/null | grep -q "^xr:" && kind=xr
  awk -F'\t' -v s="$(basename "$s")" -v k="$kind" '
    NR>2 { d=($5-p)*1000; n++; t+=d; if (d>25) late++; if (($3-$4)*1000>5) behind++
           seg=int(n/300); c[seg]++; st[seg]+=d }
    { p=$5 }
    END { if (!n) { printf "%s %s no frames\n", s, k; exit }
          printf "%s %-4s frames=%d fps=%.1f >25ms=%d behind=%d  per5s:", s, k, n, 1000/(t/n), late, behind
          for (i=0; i in c; i++) printf " %.0f", 1000/(st[i]/c[i]); print "" }' "$s/pace.tsv"
done
