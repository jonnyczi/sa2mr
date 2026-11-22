#!/usr/bin/env python3
"""add_to_steam: SA2 VR as a non-Steam game in each Steam user's library (the Steam Frame).

    python3 add_to_steam.py [--app ~/Applications/sa2mr] [--exe APP/sa2mr-vr.sh] [--stop-steam]
                            [--remove] [--steam-root ~/.local/share/Steam] [--recheck SECONDS]

A port of xelee's tools/frame_shortcut.py. Each userdata/<id>/config/shortcuts.vdf (the binary VDF
Steam's "Add a Non-Steam Game" writes) gets one entry named "Sonic Adventure 2 VR" whose Exe is the
launch script, with OpenVR=1 so the headset shows it as a VR app. An entry with the same Exe is
replaced; every other entry is kept as it was (each value keeps its VDF type). The old file is
kept as shortcuts.vdf.bak; the new one is written to a temporary file, synced and renamed over it.
--remove takes the entry out instead (the uninstaller).

Steam reads the file when it starts and may write its own copy back when it quits, so the file
must be written while Steam is stopped. --stop-steam does that: `systemctl --user stop
steam.service` (or `steam -shutdown` without the unit), waits for the client to exit, writes, then
starts it again (and, with --recheck, checks after that many seconds that the entry is still
there). Without --stop-steam a running Steam is only warned about.

The entry's appid is Steam's own for a shortcut: crc32(exe + name) | 0x80000000; it prints
steam://rungameid/<appid << 32 | 0x02000000>, and SA2MR_STEAM_ENTRY_OK at the end.
"""
from __future__ import annotations

import argparse
import binascii
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

NAME = "Sonic Adventure 2 VR"
UNIT = "steam.service"


# ---------------------------------------------------------------- binary VDF

class U64(int):
    """A 0x07 (uint64) value, written back as one."""


class F32(float):
    """A 0x03 (float32) value, written back as one."""


def read_map(data: bytes, i: int) -> tuple[dict, int]:
    """Binary VDF: 0x00 key (nested map) ... 0x08; 0x01 key string; 0x02 key int32; 0x03 float32;
    0x07 uint64; 0x08 ends a map."""
    out = {}
    while True:
        kind = data[i]
        i += 1
        if kind == 0x08:
            return out, i
        end = data.index(b"\0", i)
        key = data[i:end].decode("utf-8", "surrogateescape")
        i = end + 1
        if kind == 0x00:
            out[key], i = read_map(data, i)
        elif kind == 0x01:
            end = data.index(b"\0", i)
            out[key] = data[i:end].decode("utf-8", "surrogateescape")
            i = end + 1
        elif kind == 0x02:
            out[key] = struct.unpack_from("<i", data, i)[0]
            i += 4
        elif kind == 0x03:
            out[key] = F32(struct.unpack_from("<f", data, i)[0])
            i += 4
        elif kind == 0x07:
            out[key] = U64(struct.unpack_from("<Q", data, i)[0])
            i += 8
        else:
            raise ValueError(f"unknown VDF field type {kind:#x} at {i - 1}")


def write_map(m: dict) -> bytes:
    out = bytearray()
    for key, value in m.items():
        k = key.encode("utf-8", "surrogateescape") + b"\0"
        if isinstance(value, dict):
            out += b"\x00" + k + write_map(value)
        elif isinstance(value, U64):
            out += b"\x07" + k + struct.pack("<Q", value)
        elif isinstance(value, F32):
            out += b"\x03" + k + struct.pack("<f", value)
        elif isinstance(value, int):
            out += b"\x02" + k + struct.pack("<i", value)
        else:
            out += b"\x01" + k + str(value).encode("utf-8", "surrogateescape") + b"\0"
    return bytes(out + b"\x08")


def parse_shortcuts(data: bytes) -> dict:
    if not data.startswith(b"\x00shortcuts\x00"):
        raise ValueError("not a binary shortcuts.vdf (no shortcuts map)")
    root, end = read_map(data, len(b"\x00shortcuts\x00"))
    if data[end:] not in (b"\x08", b""):
        raise ValueError(f"trailing bytes after the shortcuts map: {data[end:][:16]!r}")
    return root


def dump_shortcuts(root: dict) -> bytes:
    return b"\x00shortcuts\x00" + write_map(root) + b"\x08"


def shortcut_ids(exe: str, name: str) -> tuple[int, int]:
    """Steam's id for a shortcut (as Steam ROM Manager computes it): the entry's appid (signed 32
    bits, as the file stores it) and the 64-bit id steam://rungameid/ takes."""
    crc = binascii.crc32((exe + name).encode()) | 0x80000000
    return struct.unpack("<i", struct.pack("<I", crc))[0], (crc << 32) | 0x02000000


def entry(app: Path, exe: str, name: str) -> dict:
    appid, _ = shortcut_ids(exe, name)
    icon = app / "share" / "sa2mr.png"
    return {
        "appid": appid,
        "AppName": name,
        "Exe": exe,
        "StartDir": f'"{app}/"',
        "icon": str(icon) if icon.is_file() else "",
        "ShortcutPath": "",
        "LaunchOptions": "",
        "IsHidden": 0,
        "AllowDesktopConfig": 1,
        "AllowOverlay": 1,
        "OpenVR": 1,            # a VR app: the headset shows it, not a flat window
        "Devkit": 0,
        "DevkitGameID": "",
        "DevkitOverrideAppID": 0,
        "LastPlayTime": 0,
        "FlatpakAppID": "",
        "sortas": "",
        "tags": {},
    }


def update(data: bytes | None, new: dict | None, exe: str) -> tuple[bytes, int]:
    """The file with `new` in place of any entry whose Exe is `exe` (or that entry removed, with
    `new` None), other entries kept in their order and renumbered; (bytes, entries)."""
    root = parse_shortcuts(data) if data else {}
    kept = [v for v in root.values() if not (isinstance(v, dict) and v.get("Exe") == exe)]
    if new is not None:
        kept.append(new)
    tree = {str(i): e for i, e in enumerate(kept)}
    return dump_shortcuts(tree), len(kept)


def write_atomic(path: Path, data: bytes) -> None:
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)
    try:
        fd = os.open(path.parent, os.O_RDONLY)
        os.fsync(fd)
        os.close(fd)
    except OSError:
        pass


# ---------------------------------------------------------------- Steam itself

def steam_running() -> bool:
    if not shutil.which("pgrep"):
        return False
    for name in ("steam", "steamwebhelper"):
        r = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", name], capture_output=True)
        if r.returncode == 0:
            return True
    return False


def has_unit() -> bool:
    if not shutil.which("systemctl"):
        return False
    r = subprocess.run(["systemctl", "--user", "cat", UNIT], capture_output=True)
    return r.returncode == 0


def stop_steam(timeout: float = 90) -> bool:
    """Stops the Steam client; True when it was running (so it is started again)."""
    if not steam_running():
        return False
    print("stopping Steam ...", flush=True)
    unit = has_unit()
    if unit:
        subprocess.run(["systemctl", "--user", "stop", UNIT], check=False)
    elif shutil.which("steam"):
        subprocess.run(["steam", "-shutdown"], check=False, stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while steam_running():
        if time.time() - t0 > timeout:
            raise SystemExit(f"add_to_steam: Steam did not exit within {timeout:.0f} s; nothing written")
        time.sleep(1)
    time.sleep(2)   # it writes its files on the way out
    return True


def start_steam() -> None:
    print("starting Steam ...", flush=True)
    if has_unit():
        subprocess.run(["systemctl", "--user", "start", UNIT], check=False)
    elif shutil.which("steam"):
        subprocess.Popen(["steam"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--app", default="~/Applications/sa2mr")
    ap.add_argument("--exe", help="the launch script (default APP/sa2mr-vr.sh)")
    ap.add_argument("--name", default=NAME)
    ap.add_argument("--steam-root", default="~/.local/share/Steam")
    ap.add_argument("--stop-steam", action="store_true", help="stop Steam, write, start it again")
    ap.add_argument("--recheck", type=float, default=0,
                    help="with --stop-steam: seconds after the restart to check the entry is still there")
    ap.add_argument("--remove", action="store_true", help="remove the entry instead")
    a = ap.parse_args(argv)
    app = Path(a.app).expanduser().resolve()
    script = Path(a.exe).expanduser().resolve() if a.exe else app / "sa2mr-vr.sh"
    if not a.remove and not script.is_file():
        raise SystemExit(f"add_to_steam: no {script}: install the app first")
    exe = f'"{script}"'
    appid, game_id = shortcut_ids(exe, a.name)
    users = sorted(Path(a.steam_root).expanduser().glob("userdata/*/config"))
    if not users:
        raise SystemExit(f"add_to_steam: no Steam user under {Path(a.steam_root).expanduser()}/userdata")
    restart = False
    if a.stop_steam:
        restart = stop_steam()
    elif steam_running():
        print("warning: Steam is running and may overwrite shortcuts.vdf when it quits "
              "(--stop-steam stops it first)", file=sys.stderr)
    try:
        for config in users:
            path = config / "shortcuts.vdf"
            data = path.read_bytes() if path.is_file() else None
            try:
                new, count = update(data, None if a.remove else entry(app, exe, a.name), exe)
            except (ValueError, IndexError) as e:
                raise SystemExit(f"add_to_steam: {path}: {e}; left as it is")
            if data is not None:
                write_atomic(path.with_name("shortcuts.vdf.bak"), data)
            write_atomic(path, new)
            print(f"{path}: {count} shortcut(s), {a.name} {'removed' if a.remove else 'added'}")
    finally:
        if restart:
            start_steam()
    if restart and a.recheck and not a.remove:
        time.sleep(a.recheck)
        for config in users:
            root = parse_shortcuts((config / "shortcuts.vdf").read_bytes())
            if not any(isinstance(v, dict) and v.get("Exe") == exe and v.get("OpenVR") == 1 for v in root.values()):
                raise SystemExit(f"add_to_steam: Steam rewrote {config / 'shortcuts.vdf'} without the entry; "
                                 "run this again with Steam stopped")
    if not a.remove:
        print(f"appid {appid & 0xFFFFFFFF} steam://rungameid/{game_id}")
    print("SA2MR_STEAM_ENTRY_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
