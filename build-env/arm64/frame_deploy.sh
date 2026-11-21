#!/usr/bin/env bash
# Puts the aarch64 OpenSA2 release on a Steam Frame in Developer Mode, over SSH, and runs it
# (mirrors ../xelee/tools/frame_deploy.sh):
#
#   build-env/arm64/frame_deploy.sh deploy       the tarball into ~/Applications/sa2mr (a share/game.env
#                                                edited on the headset is kept)
#   build-env/arm64/frame_deploy.sh install      bin/sa2-install --source steam --yes (SA2 must be
#                                                installed through Steam on the headset)
#   build-env/arm64/frame_deploy.sh selftest     bin/sa2recomp --selftest in Steam's environment
#   build-env/arm64/frame_deploy.sh run [args]   starts bin/sa2recomp in the running Steam client's
#                                                environment (display, audio, XR runtime)
#   build-env/arm64/frame_deploy.sh stop         ends the game, the player and the launcher
#   build-env/arm64/frame_deploy.sh log          the newest session's game.log and player.err and the
#                                                launcher's console, as they grow
#   build-env/arm64/frame_deploy.sh fetch DIR    the newest session directory and the console log
#
# FRAME_ENV adds variables to the game's environment for run (e.g. FRAME_ENV="SA2_XR=1 SA2_XR_MODE=diorama").
# FRAME_HOST is the headset's SSH destination (default steamos@frame.lan); FRAME_PACKAGE the
# tarball (default the release build's).
set -euo pipefail

HOST="${FRAME_HOST:-steamos@frame.lan}"
DEST="${FRAME_DIR:-Applications/sa2mr}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PACKAGE="${FRAME_PACKAGE:-$ROOT/external/OpenSA2/private/generated/s7/release/sa2mr-linux-aarch64.tar.gz}"
SESSIONS='$HOME/.local/share/sa2recomp/sessions'

remote() { ssh -o BatchMode=yes "${HOST}" "$@"; }
# The running Steam client's environment, as Steam would start a game in it.
STEAM_ENV="pid=\$(pgrep -n -u \"\$USER\" -x steam || pgrep -n -u \"\$USER\" steamwebhelper); \
    [ -n \"\$pid\" ] || { echo 'Steam is not running on the headset' >&2; exit 1; }; \
    while IFS= read -r -d '' kv; do case \"\$kv\" in \
        DISPLAY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*|PULSE_SERVER=*|XR_RUNTIME_JSON=*|STEAM_*|SteamGameId=*) export \"\$kv\";; \
    esac; done < /proc/\$pid/environ"
STOP="for n in sa2recomp sa2_player_gpu sa2_player_xr sa2_player sa2-game; do \
    for p in \$(pgrep -u \"\$USER\" -x \$n); do \
      case \"\$(readlink /proc/\$p/exe)\" in \"\$HOME/${DEST}\"/bin/*) kill \$p;; esac; done; done; true"

case "${1:-}" in
deploy)
    [[ -f "${PACKAGE}" ]] || { echo "no ${PACKAGE}: run private/sa2mr/arm64/release_build_arm64.py" >&2; exit 1; }
    stage="$(mktemp -d)"
    trap 'rm -rf "${stage}"' EXIT
    tar -xzf "${PACKAGE}" -C "${stage}"
    remote "mkdir -p '${DEST}/logs'"
    rsync -a --delete --filter='P share/game.env' --filter='P logs/' --exclude share/game.env \
        "${stage}/sa2mr/" "${HOST}:${DEST}/"
    rsync -a --ignore-existing "${stage}/sa2mr/share/game.env" "${HOST}:${DEST}/share/game.env"
    echo "deployed to ${HOST}:${DEST}"
    ;;
install)
    remote "cd '${DEST}' && bin/sa2-install --source steam --yes"
    ;;
selftest)
    shift
    remote "${STEAM_ENV}; ${STOP}; cd '${DEST}' && bin/sa2recomp --selftest $*"
    ;;
run)
    shift
    remote "${STEAM_ENV}; ${STOP}; cd '${DEST}' || exit 1; mkdir -p logs; \
        nohup env ${FRAME_ENV:-} bin/sa2recomp $* > logs/console.txt 2>&1 < /dev/null &"
    echo "started on ${HOST}"
    ;;
stop)
    remote "${STOP}"
    ;;
log)
    remote "s=\$(ls -1dt ${SESSIONS}/*/ | head -n1); tail -n +1 -F \"\$s\"game.log \"\$s\"player.err '${DEST}'/logs/console.txt"
    ;;
fetch)
    out="${2:?usage: fetch DIR}"
    mkdir -p "${out}"
    s="$(remote "ls -1dt ${SESSIONS}/*/ | head -n1")"
    rsync -a "${HOST}:${s}" "${out}/session/" || true
    rsync -a "${HOST}:${DEST}/logs/" "${out}/logs/" || true
    remote "coredumpctl list --no-pager 2>/dev/null | tail -n 5" > "${out}/coredumps.txt" || true
    echo "fetched into ${out}"
    ;;
*)
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
