#!/usr/bin/env bash
# Install, update or uninstall Sonic Adventure 2 VR on a Steam Frame (SteamOS, aarch64).
#
# Run by Install-SA2MR.desktop (a release asset: open it in desktop mode), or by hand:
#   curl -fsSL https://raw.githubusercontent.com/jonnyczi/sa2mr/main/platforms/steam-frame/install.sh -o /tmp/sa2mr-install.sh
#   bash /tmp/sa2mr-install.sh [--uninstall [--purge] | --status]
#
# What it does:
#   1. finds the newest release of jonnyczi/sa2mr that has sa2mr-app-linux-aarch64.tar.gz
#      (prereleases count; drafts don't), downloads it and SHA256SUMS into
#      ~/.local/share/sa2mr/downloads (never /tmp: the Frame's /tmp is a 7.7 GB tmpfs) and checks it;
#   2. unpacks it under ~/.local/share/sa2mr and puts it in ~/Applications/sa2mr (rsync --delete,
#      keeping the device's own bin/sa2-game, logs/, share/game.env.local and .release);
#   3. starts the builder (frame/sa2mr_build.py) as a detached systemd user unit, sa2mr-build,
#      which translates the game from the user's own Steam copy of SA2 (about an hour the first
#      time; minutes for an update that only relinks) and adds "Sonic Adventure 2 VR" to Steam;
#      this terminal only follows its progress, so closing it does not stop the build, and
#      running the installer again attaches to a build that is still running.
# The release holds no game code or data: only our programs, the public OpenSA2 source and the
# open-source toolchain (fetched by the builder).
#
# Options:
#   --status      follow a running build (or show how the last one ended); change nothing
#   --uninstall   stop the build, remove the Steam entry, ~/Applications/sa2mr and
#                 ~/.local/share/sa2mr (the toolchain, build cache, logs); keep the saves
#   --purge       with --uninstall: also the saves and settings (~/.local/share/sa2recomp,
#                 ~/.config/sa2recomp, ~/.config/sa2mr); asks first (SA2MR_YES=1 skips that)
# Knobs (tests and special cases):
#   SA2MR_REPO=owner/name   where the releases are (default jonnyczi/sa2mr)
#   SA2MR_TAG=tag           that release instead of the newest
#   SA2MR_PACKAGE=file|URL  this app tarball instead of a release's (SA2MR_SUMS=file checks it)
#   SA2MR_DIR=dir           where the app goes (default ~/Applications/sa2mr)
#   SA2MR_DATA=dir          the build data (default ~/.local/share/sa2mr)
#   SA2MR_NO_STEAM=1        no Steam entry
#   SA2MR_JOBS=N            the builder's parallel jobs (default: from the CPUs and memory)
#   SA2MR_FOREGROUND=1      run the builder in this terminal instead of a systemd unit
#   SA2MR_ALLOW_ANY_ARCH=1  run on a machine that is not aarch64 (tests)
#   SA2MR_BUILD_ARGS="..."  more arguments for sa2mr_build.py (tests: --no-wait-install, --dry-run)
set -euo pipefail

REPO="${SA2MR_REPO:-jonnyczi/sa2mr}"
DEST="${SA2MR_DIR:-$HOME/Applications/sa2mr}"
DATA="${SA2MR_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/sa2mr}"
ASSET="sa2mr-app-linux-aarch64.tar.gz"
SUMS="SHA256SUMS"
UNIT="sa2mr-build"
NAME="Sonic Adventure 2 VR"
PROGRESS="$DATA/state/progress.json"

MODE=install PURGE=0
for arg in "$@"; do
    case "$arg" in
        --uninstall) MODE=uninstall ;;
        --purge) PURGE=1 ;;
        --status) MODE=status ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "install.sh: unknown option $arg (try --help)" >&2; exit 2 ;;
    esac
done
[[ "$PURGE" == 0 || "$MODE" == uninstall ]] || { echo "install.sh: --purge goes with --uninstall" >&2; exit 2; }

say() { printf '%s\n' "$@"; }
desktop() { [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; }
tell() {  # a desktop notice when started from the .desktop file
    [[ -n "${SA2MR_FROM_DESKTOP:-}" ]] || return 0
    if command -v kdialog >/dev/null && desktop; then
        kdialog --title "$NAME" "--$1" "$2" >/dev/null 2>&1 &
    elif command -v notify-send >/dev/null; then
        notify-send -a "$NAME" "$NAME" "$2" >/dev/null 2>&1 || true
    fi
}
finish() {
    if [[ -n "${SA2MR_FROM_DESKTOP:-}" ]]; then
        say "" "You can close this window."
    fi
    exit "${1:-0}"
}
fail() {
    say "" "Error: $*" >&2
    tell error "$*"
    finish 1
}
unit_active() { systemctl --user is-active --quiet "$UNIT.service" 2>/dev/null; }
have_systemd() { command -v systemd-run >/dev/null && systemctl --user show-environment >/dev/null 2>&1; }

# A process whose executable is in the app (the game, a player, the launcher) keeps its files open.
game_running() {
    local p exe
    for p in /proc/[0-9]*; do
        exe="$(readlink "$p/exe" 2>/dev/null || true)"
        [[ -n "$exe" && "$exe" == "$DEST"/* ]] && return 0
    done
    return 1
}

# ---------------------------------------------------------------- following the build

progress_line() {
    [[ -f "$PROGRESS" ]] || return 0
    python3 - "$PROGRESS" <<'EOF' 2>/dev/null || true
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)
parts = [f"[{p.get('step', '?')}/14] {p.get('title', '')}", p.get("detail", ""), p.get("eta", "")]
print("  ".join(x for x in parts if x).replace("\n", " ")[:200])
EOF
}
progress_field() {
    [[ -f "$PROGRESS" ]] || return 0
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))' "$PROGRESS" "$1" 2>/dev/null || true
}

# Follow the unit until it ends; then report how the build ended. $1: the time it was started
# (progress.json older than that is a previous build's).
follow() {
    local since="${1:-0}" last="" line
    say "Following the build (closing this window does not stop it; run the installer again to come back)."
    trap 'say "" "The build goes on in the background. Run the installer again to follow it."; exit 0' INT
    while unit_active; do
        line="$(progress_line)"
        if [[ -n "$line" && "$line" != "$last" ]]; then
            say "$line"
            last="$line"
        fi
        sleep 5
    done
    trap - INT
    report "$since"
}

report() {
    local since="${1:-0}" state log mtime
    mtime="$(stat -c %Y "$PROGRESS" 2>/dev/null || echo 0)"
    state="$(progress_field state)"
    log="$(progress_field log)"
    if [[ "$mtime" -lt "$since" ]]; then
        state=""
    fi
    case "$state" in
        done)
            say "" "SA2 VR is ready: start \"$NAME\" from your Steam library (it runs in VR)."
            tell msgbox "$NAME is ready. Start it from your Steam library."
            finish 0 ;;
        failed)
            say "" "The build stopped: $(progress_field title)" "$(progress_field detail)" ""
            [[ -n "$log" && -f "$log" ]] && { say "Last lines of $log:"; tail -n 25 "$log"; }
            fail "the build failed (log: ${log:-$DATA/logs}). Run the installer again to resume where it stopped." ;;
        *)
            say "" "The build is not running and left no result; its log: $DATA/logs/build-latest.log"
            finish 1 ;;
    esac
}

start_builder() {
    local args=(--kit "$DEST/kit" --toolchain "$DATA/toolchain" --app "$DEST" --data "$DATA")
    [[ -n "${SA2MR_NO_STEAM:-}" ]] && args+=(--no-steam)
    [[ -n "${SA2MR_ALLOW_ANY_ARCH:-}" ]] && args+=(--allow-any-arch)
    local extra
    read -r -a extra <<< "${SA2MR_BUILD_ARGS:-}"
    args+=("${extra[@]}")
    local py=/usr/bin/python3
    [[ -x "$py" ]] || py="$(command -v python3)"
    mkdir -p "$DATA"
    local since
    since="$(date +%s)"
    if [[ -n "${SA2MR_FOREGROUND:-}" ]] || ! have_systemd; then
        have_systemd || say "(no systemd user session: the build runs in this terminal; keep it open)"
        local inhibit=()
        command -v systemd-inhibit >/dev/null && inhibit=(systemd-inhibit --what=sleep:idle --why="SA2 VR build")
        (cd "$DATA" && nice -n 10 "${inhibit[@]}" "$py" "$DEST/frame/sa2mr_build.py" "${args[@]}") || true
        report "$since"
    fi
    local envs=()
    local v
    for v in SA2MR_JOBS DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS; do
        [[ -n "${!v:-}" ]] && envs+=(-E "$v=${!v}")
    done
    systemctl --user reset-failed "$UNIT.service" >/dev/null 2>&1 || true
    systemd-run --user --unit="$UNIT" --collect -p Nice=10 -p IOSchedulingClass=idle -p MemoryHigh=12G \
        --working-directory="$DATA" "${envs[@]}" \
        systemd-inhibit --what=sleep:idle --why="SA2 VR build" \
        "$py" "$DEST/frame/sa2mr_build.py" "${args[@]}" >/dev/null \
        || fail "couldn't start the build (systemd-run)."
    say "The build runs in the background (systemd user unit $UNIT; journalctl --user -u $UNIT)."
    sleep 2
    follow "$since"
}

# ---------------------------------------------------------------- status

if [[ "$MODE" == status ]]; then
    if unit_active; then
        follow 0
    fi
    if [[ -f "$PROGRESS" ]]; then
        say "The last build: $(progress_line) ($(progress_field state))"
    else
        say "No build has run here ($PROGRESS is missing)."
    fi
    [[ -f "$DEST/.release" ]] && say "Installed release: $(cat "$DEST/.release") in $DEST"
    finish 0
fi

# ---------------------------------------------------------------- uninstall

if [[ "$MODE" == uninstall ]]; then
    say "Uninstalling $NAME."
    if unit_active; then
        say "Stopping the build..."
        systemctl --user stop "$UNIT.service" || true
    fi
    game_running && fail "SA2 VR is running; close it, then run the uninstaller again."
    if [[ "$PURGE" == 1 && -z "${SA2MR_YES:-}" ]]; then
        q="--purge also deletes your SA2 saves and settings (~/.local/share/sa2recomp, ~/.config/sa2recomp, ~/.config/sa2mr). Delete them?"
        if [[ -t 0 ]]; then
            read -r -p "$q [y/N] " ans
            [[ "$ans" == [yY]* ]] || { say "Nothing deleted."; finish 1; }
        elif command -v kdialog >/dev/null && desktop; then
            kdialog --title "$NAME" --warningyesno "$q" >/dev/null 2>&1 || { say "Nothing deleted."; finish 1; }
        else
            fail "--purge needs a confirmation: run it in a terminal, or set SA2MR_YES=1."
        fi
    fi
    if [[ -f "$DEST/frame/add_to_steam.py" && -z "${SA2MR_NO_STEAM:-}" ]]; then
        say "Removing the Steam entry (Steam closes for a moment and starts again)..."
        python3 "$DEST/frame/add_to_steam.py" --remove --app "$DEST" --exe "$DEST/sa2mr-vr.sh" --stop-steam \
            || say "Couldn't remove the Steam entry; remove \"$NAME\" from your Steam library by hand."
    fi
    rm -rf "$DEST" "$DATA"
    say "Removed $DEST and $DATA."
    if [[ "$PURGE" == 1 ]]; then
        rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/sa2recomp" "${XDG_CONFIG_HOME:-$HOME/.config}/sa2recomp" \
               "${XDG_CONFIG_HOME:-$HOME/.config}/sa2mr"
        say "Removed the saves and settings."
    else
        say "Your saves and settings are kept (~/.local/share/sa2recomp, ~/.config/sa2mr)."
    fi
    tell msgbox "$NAME is uninstalled."
    finish 0
fi

# ---------------------------------------------------------------- install or update

if [[ "$(uname -m)" != aarch64 && -z "${SA2MR_ALLOW_ANY_ARCH:-}" ]]; then
    fail "this installs the Steam Frame build (Linux arm64); this machine is $(uname -m)."
fi
for tool in curl tar rsync python3 sha256sum; do
    command -v "$tool" >/dev/null || fail "$tool is missing."
done

if unit_active; then
    say "A build is already running: attaching to it."
    follow 0
fi

DL="$DATA/downloads"
mkdir -p "$DL"
case "$DL" in /tmp/*) fail "the downloads must not go to /tmp (a small tmpfs on the Frame): set SA2MR_DATA." ;; esac
pkg="" sums="" tag=""

if [[ -n "${SA2MR_PACKAGE:-}" ]]; then
    tag="local:$(basename "$SA2MR_PACKAGE")"
    say "Package: $SA2MR_PACKAGE"
    if [[ -f "$SA2MR_PACKAGE" ]]; then
        pkg="$SA2MR_PACKAGE"
    else
        pkg="$DL/local/$ASSET"
        mkdir -p "$DL/local"
        curl -fL --retry 3 --progress-bar -o "$pkg" "$SA2MR_PACKAGE" || fail "couldn't download $SA2MR_PACKAGE."
    fi
    sums="${SA2MR_SUMS:-}"
else
    say "Looking for the newest $NAME release (github.com/$REPO)..."
    if [[ -n "${SA2MR_TAG:-}" ]]; then
        api="https://api.github.com/repos/$REPO/releases/tags/$SA2MR_TAG"
    else
        api="https://api.github.com/repos/$REPO/releases?per_page=30"
    fi
    curl -fsSL --retry 3 -H "Accept: application/vnd.github+json" "$api" -o "$DL/releases.json" \
        || fail "couldn't reach GitHub (is the Frame online?)."
    read -r tag url sums_url < <(python3 - "$DL/releases.json" "$ASSET" "$SUMS" <<'EOF'
import json, sys
data, asset, sums = json.load(open(sys.argv[1])), sys.argv[2], sys.argv[3]
for r in data if isinstance(data, list) else [data]:
    if r.get("draft"):
        continue
    assets = {a.get("name"): a.get("browser_download_url") for a in r.get("assets", [])}
    if asset in assets and sums in assets:
        print(r["tag_name"], assets[asset], assets[sums])
        sys.exit(0)
print("- - -")
EOF
    ) || true
    [[ -n "$tag" && "$tag" != - ]] || fail "no release of $REPO has $ASSET and $SUMS yet."
    say "Release: $tag"
fi

# The same release again: nothing to download. If its build finished, it is up to date;
# otherwise the build resumes where it stopped.
same=false
[[ -f "$DEST/.release" && "$(cat "$DEST/.release")" == "$tag" && -x "$DEST/bin/sa2recomp" ]] && same=true
if $same && [[ -x "$DEST/bin/sa2-game" && "$(progress_field state)" == "done" ]]; then
    say "$NAME $tag is installed and built: up to date."
    say "Start it from your Steam library. (To rebuild, run: bash $0 --uninstall, then install again.)"
    finish 0
fi

if ! $same; then
    if [[ -z "$pkg" ]]; then
        d="$DL/$tag"
        mkdir -p "$d"
        say "Downloading $ASSET..."
        curl -fL --retry 5 -C - --progress-bar -o "$d/$ASSET" "$url" \
            || { rm -f "$d/$ASSET"; curl -fL --retry 5 --progress-bar -o "$d/$ASSET" "$url"; } \
            || fail "the download failed; run the installer again."
        curl -fsSL --retry 3 -o "$d/$SUMS" "$sums_url" || fail "couldn't download $SUMS."
        pkg="$d/$ASSET" sums="$d/$SUMS"
    fi
    if [[ -n "$sums" ]]; then
        want="$(awk -v a="$ASSET" '$2 == a || $2 == "*"a {print $1}' "$sums")"
        have="$(sha256sum "$pkg" | cut -d' ' -f1)"
        [[ -n "$want" ]] || fail "$SUMS names no $ASSET."
        if [[ "$have" != "$want" ]]; then
            [[ -z "${SA2MR_PACKAGE:-}" ]] && rm -f "$pkg"
            fail "$ASSET is damaged (sha256 $have, $SUMS says $want); run the installer again."
        fi
        say "Checked: sha256 ${have:0:16}... matches $SUMS."
    else
        say "(a local package without SA2MR_SUMS: its sha256 is not checked)"
    fi

    game_running && fail "SA2 VR is running; close it, then run the installer again."

    say "Unpacking..."
    stage="$(mktemp -d "$DATA/unpack.XXXXXX")"
    trap 'rm -rf "$stage"' EXIT
    tar -xzf "$pkg" -C "$stage" || fail "the package is damaged; run the installer again."
    for f in bin/sa2recomp bin/sa2-install frame/sa2mr_build.py kit/tools/s4/rebuild_image.py sa2mr-vr.sh; do
        [[ -e "$stage/sa2mr/$f" ]] || fail "the package lacks sa2mr/$f."
    done
    [[ ! -e "$stage/sa2mr/bin/sa2-game" ]] || fail "the package holds bin/sa2-game, which a release never ships."

    update=false
    [[ -d "$DEST" ]] && update=true
    mkdir -p "$DEST"
    # Kept: what the device built or the user wrote. The builder relinks bin/sa2-game when the new
    # kit or toolchain needs it (sa2mr_build.py's translation key).
    rsync -a --delete \
        --exclude /bin/sa2-game --exclude /logs/ --exclude /share/game.env.local --exclude /.release \
        --exclude /share/sa2-game.build.json --exclude /share/cpu-needs.txt \
        "$stage/sa2mr/" "$DEST/"
    printf '%s\n' "$tag" > "$DEST/.release"
    rm -rf "$stage"
    trap - EXIT
    # Older downloads go once the new one is in place.
    for d in "$DL"/*/; do
        [[ -d "$d" && "$(basename "$d")" != "$tag" && "$(basename "$d")" != local ]] && rm -rf "$d"
    done
    say "$($update && echo Updated || echo Installed) $NAME $tag in $DEST."
else
    say "$NAME $tag is installed; its build has not finished: resuming it."
fi

say "" "Building SA2 VR from your own copy of Sonic Adventure 2 (installed through Steam)."
say "The first time takes about an hour and needs about 25 GB free; keep the Frame on its charger."
start_builder
