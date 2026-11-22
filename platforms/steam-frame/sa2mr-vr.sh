#!/usr/bin/env bash
# Sonic Adventure 2 VR on the Steam Frame: the Steam entry's program (add_to_steam.py, OpenVR=1).
# Steam starts it with its own environment (display, audio, the SteamVR OpenXR runtime), as
# build-env/arm64/frame_deploy.sh `run` reproduces over SSH.
#
#   1. Checks that the game is built (bin/sa2-game, linked on the device by sa2mr_build.py) and
#      that the game files are verified (sa2-install's manifest); otherwise it says to run the
#      installer again.
#   2. share/game.env, the file the launcher reads (and passes to the game as written), is made
#      from share/game.env.vr (the VR settings the app ships, platforms/steam-frame/game.env) with
#      the user's ~/.config/sa2mr/game.env.local on top: a line there replaces the shipped line
#      of the same name, or is added. Without game.env.vr, share/game.env is left as it is.
#   3. SA2_XR=1 (the launcher starts the VR player, host/xr) and the SA2_XR* lines of the merged
#      file in the environment (the launcher passes SA2_XR* through to the player); then
#      bin/sa2recomp with this script's arguments, its output appended to logs/launch.log.
set -u
HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
LOCAL="${XDG_CONFIG_HOME:-$HOME/.config}/sa2mr/game.env.local"
MANIFEST="${XDG_DATA_HOME:-$HOME/.local/share}/sa2recomp/manifest.tsv"
LOG="$HERE/logs/launch.log"
mkdir -p "$HERE/logs"

fail() {
    echo "sa2mr-vr: $1" >&2
    echo "$(date '+%F %T') sa2mr-vr: $1" >> "$LOG"
    if command -v kdialog >/dev/null && { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }; then
        kdialog --title "Sonic Adventure 2 VR" --error "$1" >/dev/null 2>&1 &
    elif command -v notify-send >/dev/null; then
        notify-send -a "Sonic Adventure 2 VR" "Sonic Adventure 2 VR" "$1"
    fi
    exit 1
}

[ -x "$HERE/bin/sa2recomp" ] || fail "The app is incomplete ($HERE/bin/sa2recomp is missing). Run the installer (Install-SA2MR.desktop) again."
[ -x "$HERE/bin/sa2-game" ] || fail "The game has not been built on this device yet. Run the installer (Install-SA2MR.desktop) again; it resumes the build."
[ -f "$MANIFEST" ] || fail "The game files have not been verified ($MANIFEST is missing). Run the installer (Install-SA2MR.desktop) again."

ENV="$HERE/share/game.env"
if [ -f "$HERE/share/game.env.vr" ]; then
    tmp="$ENV.tmp.$$"
    if [ -f "$LOCAL" ]; then
        # Later lines win: the shipped file, then the user's; comments and blank lines kept from the shipped one.
        awk -F= '
            FNR == 1 { file++ }
            /^[ \t]*#/ || !/=/ { if (file == 1) order[++n] = $0; next }
            { k = $1; if (!(k in val)) order[++n] = "\001" k; val[k] = $0 }
            END { for (i = 1; i <= n; i++) { l = order[i]; if (substr(l, 1, 1) == "\001") print val[substr(l, 2)]; else print l } }
        ' "$HERE/share/game.env.vr" "$LOCAL" > "$tmp"
    else
        cp "$HERE/share/game.env.vr" "$tmp"
    fi
    mv -f "$tmp" "$ENV"
fi

export SA2_XR=1
if [ -f "$ENV" ]; then
    while IFS= read -r line; do
        case "$line" in SA2_XR*=*) export "${line?}";; esac
    done < "$ENV"
fi

cd "$HERE" || fail "cannot enter $HERE"
{
    echo "=== $(date '+%F %T') sa2mr-vr: bin/sa2recomp $* (SA2_XR=1)"
} >> "$LOG"
exec "$HERE/bin/sa2recomp" "$@" >> "$LOG" 2>&1 < /dev/null
