#!/usr/bin/env python3
"""sa2mr_build: build Sonic Adventure 2 VR on the Steam Frame from the user's own Steam copy.

    python3 sa2mr_build.py --kit DIR --toolchain DIR [--app DIR] [--data DIR] [--jobs N]
                           [--from-step N] [--only N,M,...] [--dry-run]

SA2 has no source: its game code exists only in the user's `sonic2app.exe`, so the playable
`bin/sa2-game` is translated here, on the device (Steam Frame install plan, Phase 4). Nothing this
script makes leaves the device. Python 3.12 standard library only.

Inputs
  --kit DIR        an unpacked OpenSA2 source tree (`git archive` of the jonnyczi/OpenSA2 sa2mr
                   branch: the pipeline tools, sa2mr/, the trimmed function map, the receipts)
  --toolchain DIR  remill/ (bin/remill-lift-14, share/remill/14/semantics, include/), zig/zig, wheels/*.whl
                   (build-env/frame-toolchain in the root repo); a kit toolchain.json ({"url",
                   "sha256"}) lets step 1 fetch it when DIR is empty
  --app DIR        the installed app (~/Applications/sa2mr): bin/sa2-install, bin/sa2recomp, ...;
                   step 13 puts bin/sa2-game here
  --data DIR       ~/.local/share/sa2mr: work/ (temporary, about 20 GB, never /tmp: the Frame's
                   /tmp is a 7.7 GB tmpfs), cache/ (about 600 MB between builds), state/, logs/, .venv

Steps (each one's state is <data>/state/steps/NN-name.json, keyed by a digest of its inputs, so a
re-run resumes after the last finished step; --from-step N reruns from N, --only runs just those)
   0 preflight   aarch64, free disk, RAM, unshare, SA2 installed through Steam (else
                 steam://install/213610 and wait), the battery
   1 toolchain   the toolchain's layout and MANIFEST.sha256 (fetched first when the kit names one)
   2 venv        <data>/.venv from the toolchain's wheels (pefile, iced-x86), no network
   3 install     <app>/bin/sa2-install --source steam --yes; the verified game folder from its
                 manifest (~/.local/share/sa2recomp/manifest.tsv), sonic2app.exe = 4b1e09d7...
   4 worktree    the kit copied to <data>/work/opensa2, the shipped exe at private/exe/, .venv
   5 unwrap      Steamless 3.1.0.5 (downloaded, sha256-checked) under Proton 11 (ARM64) in the
                 Steam Linux Runtime 4: private/exe/sonic2app-unpacked.exe = c97b116f...; then
                 sa2mr/map/restore_map.py: the full-shape function map from the trimmed one
   6 translate   tools/s4/rebuild_image.py --jobs N (lift, pass, fold, compile: the long step)
   7 receipt     tools/s4/lift_ir_receipt.py must print LIFT_IR_OK (a hard stop)
   8 link-inputs mb_edges.py, gen_table.py, drm_unwrap.py --companion, sa2mr/finalize_extras.py
   9 aarch64     sa2mr/arm64/lift_arm64.py --target aarch64-linux-musl --mode double
  10 tables      sa2mr/arm64/release_build_arm64.py --only tables (install and overlay tables)
  11 cache       the keep-list (CACHE_KEEP) hard-linked into <data>/cache/<translation key>
  12 cleanup     <data>/work deleted
  13 link        a link tree (kit + cache) and release_build_arm64.py --only game --f80-cached;
                 bin/sa2-game and share/cpu-needs.txt into <app>; older caches dropped
  14 steam       add_to_steam.py (Steam stopped, the entry written with OpenVR=1, restarted),
                 notify-send

The translation key is the sha256 of every kit file sa2mr/frame/translation_inputs.txt names and of
the toolchain's MANIFEST.sha256. A run whose key has a complete cache skips steps 4-12 and only
relinks (minutes), so an app update with an unchanged translation is quick.

Jobs: min(nproc - 1, (MemAvailable - 3 GB) / 0.6 GB), clamped to 1..7 (--jobs or SA2MR_JOBS
override). Progress goes to <data>/state/progress.json and one console line; the full log to
<data>/logs/build-<time>.log, with the hottest thermal zone every minute. A new step waits while
the device is hotter than SA2MR_HOT_C (90 C) until it is under SA2MR_COOL_C (80 C).
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHIPPED_SHA256 = "4b1e09d782a2d919cbed2324ba2c0c78ef5fbe1677d400edab39608191f12c16"
UNPACKED_SHA256 = "c97b116fbd1d64108cbc8ec403b4a59ba6f815159b9a0ad896668d6c19c1bfe9"
SA2_APPID = 213610
STEAMLESS_URL = ("https://github.com/atom0s/Steamless/releases/download/v3.1.0.5/"
                 "Steamless.v3.1.0.5.-.by.atom0s.zip")
STEAMLESS_SHA256 = "e3e2d22e098ff3fb359b2876aa2bed9596f0501e6ff588cbffae90a76d2dc4f5"
STEAMLESS_PLUGINS = ("Steamless.API.dll", "SharpDisasm.dll")   # must sit beside the CLI under wine-mono
PROTON_NAMES = ("Proton 11.0 (ARM64)",) if platform.machine() == "aarch64" else ("Proton 11.0",)
SLR_NAMES = ("SteamLinuxRuntime_4-arm64",) if platform.machine() == "aarch64" else ()
STEAMLESS_TIMEOUT = 900
TAG = "next+allarms"
F80_OUT = "private/generated/s8/pc-double/linux-aarch64-obj"
RELEASE_OUT = "private/generated/s7/release/linux-aarch64"
# What a relink needs once work/ is gone (release_build_arm64.py's game step): the aarch64 objects
# with their build record, the staged link directory (its object names), the function table, the
# edge table and extras state, and the install and overlay tables.
CACHE_KEEP = (F80_OUT,
              f"private/generated/s3/whole_image/build/{TAG}",
              "private/generated/s3/link",
              "private/generated/s4/link",
              "private/generated/s7/release/build/linux-aarch64/install",
              "private/generated/s7/release/build/linux-aarch64/overlay")
TRANSLATION_INPUTS = "sa2mr/frame/translation_inputs.txt"
TRIMMED_MAP = "Documentation/sa2mr/function-map-trimmed.json"
MIN_FREE_GB, MIN_FREE_RELINK_GB = 25, 3
JOB_GB, RESERVE_GB, MAX_JOBS = 0.6, 3.0, 7
NAME = "Sonic Adventure 2 VR"


class StepError(Exception):
    pass


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_text(*parts: str) -> str:
    return hashlib.sha256("\0".join(parts).encode()).hexdigest()


def meminfo_gb(key: str) -> float:
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith(key + ":"):
                return int(line.split()[1]) / (1024 * 1024)
    except OSError:
        pass
    return 0.0


def default_jobs() -> int:
    if os.environ.get("SA2MR_JOBS"):
        return max(1, int(os.environ["SA2MR_JOBS"]))
    cpus = (os.cpu_count() or 2) - 1
    by_mem = int((meminfo_gb("MemAvailable") - RESERVE_GB) / JOB_GB)
    return max(1, min(MAX_JOBS, cpus, by_mem))


def hottest_zone() -> tuple[float, str] | None:
    best = None
    for z in Path("/sys/class/thermal").glob("thermal_zone*"):
        try:
            t = int((z / "temp").read_text().strip()) / 1000.0
            kind = (z / "type").read_text().strip()
        except (OSError, ValueError):
            continue
        if best is None or t > best[0]:
            best = (t, f"{z.name} {kind}")
    return best


def on_battery() -> bool:
    for s in Path("/sys/class/power_supply").glob("*"):
        try:
            if (s / "type").read_text().strip() == "Battery" and \
                    (s / "status").read_text().strip() == "Discharging":
                return True
        except OSError:
            continue
    return False


# ---------------------------------------------------------------- Steam's files

def vdf_text_paths(text: str) -> list[str]:
    """The "path" values of a text libraryfolders.vdf."""
    return [m.group(1).replace("\\\\", "\\") for m in re.finditer(r'"path"\s+"((?:[^"\\]|\\.)*)"', text)]


def steam_libraries(steam_root: Path) -> list[Path]:
    libs = [steam_root]
    vdf = steam_root / "steamapps" / "libraryfolders.vdf"
    if vdf.is_file():
        for p in vdf_text_paths(vdf.read_text(errors="replace")):
            if Path(p) not in libs:
                libs.append(Path(p))
    return [lib for lib in libs if (lib / "steamapps").is_dir()]


def sa2_installed(steam_root: Path) -> Path | None:
    """The library holding a fully installed SA2 (appmanifest StateFlags has bit 4), or None."""
    for lib in steam_libraries(steam_root):
        acf = lib / "steamapps" / f"appmanifest_{SA2_APPID}.acf"
        if acf.is_file():
            m = re.search(r'"StateFlags"\s+"(\d+)"', acf.read_text(errors="replace"))
            if m and int(m.group(1)) & 4:
                return lib
    return None


def find_tool_dir(steam_root: Path, names: tuple, override: str | None, what: str) -> Path | None:
    if override:
        p = Path(override).expanduser()
        if not p.is_dir():
            raise StepError(f"{what}: no directory {p}")
        return p
    for lib in steam_libraries(steam_root):
        for n in names:
            p = lib / "steamapps" / "common" / n
            if p.is_dir():
                return p
    return None


def install_manifest_root() -> tuple[Path, Path]:
    """(the manifest, the game folder it verified) from sa2-install's manifest.tsv
    (host/install/sa2_scan.cpp: a `sa2recomp-manifest 1` header, then `root<TAB><dir>`)."""
    base = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share")
    manifest = base / "sa2recomp" / "manifest.tsv"
    if not manifest.is_file():
        raise StepError(f"sa2-install wrote no manifest at {manifest}")
    lines = manifest.read_text(errors="replace").splitlines()
    if not lines or lines[0].strip() != "sa2recomp-manifest 1":
        raise StepError(f"{manifest}: not a sa2recomp manifest")
    for line in lines[1:]:
        f = line.split("\t")
        if f[0] == "root" and len(f) >= 2:
            return manifest, Path(f[1])
    raise StepError(f"{manifest}: no root line")


# ---------------------------------------------------------------- the run

class Build:
    def __init__(self, a: argparse.Namespace):
        self.a = a
        self.kit = Path(a.kit).expanduser().resolve() if a.kit else None
        self.tc = Path(a.toolchain).expanduser().resolve()
        self.app = Path(a.app).expanduser().resolve()
        self.data = Path(a.data).expanduser().resolve()
        self.steam = Path(a.steam_root).expanduser()
        self.work = self.data / "work"
        self.tree = self.work / "opensa2"
        self.cache = self.data / "cache"
        self.state = self.data / "state"
        self.venv = self.data / ".venv"
        self.jobs = a.jobs or default_jobs()
        self.dry = a.dry_run
        self.log = None
        self.log_path = None
        self.status_len = 0
        self.tty = sys.stdout.isatty()
        self._tkey = None
        self._stop = threading.Event()
        self.current = None
        self._last_progress = 0.0

    # -- logging and progress
    def open_log(self) -> None:
        (self.data / "logs").mkdir(parents=True, exist_ok=True)
        self.log_path = self.data / "logs" / time.strftime("build-%Y%m%d-%H%M%S.log")
        self.log = open(self.log_path, "a", buffering=1)
        latest = self.data / "logs" / "build-latest.log"
        try:
            if latest.is_symlink() or latest.exists():
                latest.unlink()
            latest.symlink_to(self.log_path.name)
        except OSError:
            pass

    def note(self, msg: str, console: bool = True) -> None:
        line = time.strftime("%H:%M:%S ") + msg
        if self.log:
            self.log.write(line + "\n")
        if console:
            self.clear_status()
            print(msg, flush=True)

    def clear_status(self) -> None:
        if self.tty and self.status_len:
            sys.stdout.write("\r" + " " * self.status_len + "\r")
            self.status_len = 0

    def status(self, text: str) -> None:
        if not self.tty:
            return
        cols = shutil.get_terminal_size((100, 20)).columns - 1
        text = text[:cols]
        sys.stdout.write("\r" + text + " " * max(0, self.status_len - len(text)))
        sys.stdout.flush()
        self.status_len = len(text)

    def progress(self, **kv) -> None:
        if self.dry:
            return
        p = self.state / "progress.json"
        try:
            cur = json.loads(p.read_text()) if p.is_file() else {}
        except ValueError:
            cur = {}
        cur.update(kv, updated=time.strftime("%Y-%m-%dT%H:%M:%S%z"), log=str(self.log_path),
                   pid=os.getpid())
        tmp = p.with_suffix(".tmp")
        tmp.write_text(json.dumps(cur, indent=1) + "\n")
        os.replace(tmp, p)

    def thermal_logger(self) -> None:
        while not self._stop.wait(60):
            z = hottest_zone()
            mem = meminfo_gb("MemAvailable")
            self.note(f"thermal: {z[0]:.1f} C ({z[1]}); MemAvailable {mem:.1f} GB" if z
                      else f"thermal: no zone; MemAvailable {mem:.1f} GB", console=False)

    def wait_cool(self) -> None:
        hot, cool = float(os.environ.get("SA2MR_HOT_C", 90)), float(os.environ.get("SA2MR_COOL_C", 80))
        z = hottest_zone()
        if not z or z[0] < hot:
            return
        self.note(f"the device is at {z[0]:.0f} C ({z[1]}): waiting for it to cool below {cool:.0f} C")
        t0 = time.time()
        while time.time() - t0 < 1800:
            time.sleep(15)
            z = hottest_zone()
            if not z or z[0] < cool:
                return
        self.note("still hot after 30 min; going on")

    # -- step state
    def state_file(self, n: int, name: str) -> Path:
        return self.state / "steps" / f"{n:02d}-{name}.json"

    def read_state(self, n: int, name: str) -> dict:
        p = self.state_file(n, name)
        try:
            return json.loads(p.read_text()) if p.is_file() else {}
        except ValueError:
            return {}

    def write_state(self, n: int, name: str, rec: dict) -> None:
        p = self.state_file(n, name)
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_suffix(".tmp")
        tmp.write_text(json.dumps(rec, indent=1, sort_keys=True) + "\n")
        os.replace(tmp, p)

    def clear_states(self, numbers) -> None:
        for n, name, *_ in STEPS:
            if n in numbers:
                p = self.state_file(n, name)
                if p.exists() and not self.dry:
                    p.unlink()

    # -- keys
    def toolchain_digest(self) -> str:
        m = self.tc / "MANIFEST.sha256"
        if m.is_file():
            return "manifest:" + sha256_file(m)
        # A toolchain without a manifest (a local test): its two programs and the semantics.
        parts = []
        for p in [self.tc / "remill/bin/remill-lift-14", self.zig()] + \
                sorted((self.tc / "remill/share/remill/14/semantics").glob("*.bc")):
            parts.append(f"{p.name}:{sha256_file(p) if p.is_file() else 'missing'}")
        return "files:" + sha256_text(*parts)

    def translation_inputs(self) -> list[Path]:
        lst = self.kit / TRANSLATION_INPUTS
        if not lst.is_file():
            raise StepError(f"the kit has no {TRANSLATION_INPUTS}")
        files = set()
        for line in lst.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            hits = [p for p in self.kit.glob(line) if p.is_file() and "__pycache__" not in p.parts]
            if not hits and not any(c in line for c in "*?["):
                raise StepError(f"{TRANSLATION_INPUTS}: {line} is not in the kit")
            files.update(hits)
        return sorted(files)

    def tkey(self) -> str:
        if self._tkey is None:
            h = hashlib.sha256()
            for p in self.translation_inputs():
                h.update(p.relative_to(self.kit).as_posix().encode() + b"\0")
                h.update(sha256_file(p).encode() + b"\0")
            h.update(self.toolchain_digest().encode())
            self._tkey = h.hexdigest()[:32]
        return self._tkey

    def kit_digest(self) -> str:
        h = hashlib.sha256()
        for p in sorted(self.kit.rglob("*")):
            if p.is_file() and not p.is_symlink() and "__pycache__" not in p.parts:
                h.update(p.relative_to(self.kit).as_posix().encode() + b"\0" + sha256_file(p).encode())
        return h.hexdigest()[:32]

    def cache_dir(self) -> Path:
        return self.cache / self.tkey()

    def cache_ok(self) -> bool:
        c = self.cache_dir()
        try:
            rec = json.loads((c / "cache.json").read_text())
        except (OSError, ValueError):
            return False
        return rec.get("key") == self.tkey() and all((c / "files" / k).exists() for k in CACHE_KEEP)

    # -- tools
    def zig(self) -> Path:
        return self.tc / "zig" / "zig"

    def py(self) -> str:
        return str(self.venv / "bin" / "python")

    def env(self) -> dict:
        e = dict(os.environ)
        tmp = self.work / "tmp"
        if not self.dry:
            tmp.mkdir(parents=True, exist_ok=True)
        e.update({
            "SA2_ZIG": str(self.zig()),
            "SA2_REMILL_PREFIX": str(self.tc / "remill"),
            "SA2_REMILL_MODE": os.environ.get("SA2_REMILL_MODE", "unshare"),
            "SA2_CROSS": "",
            "SA2_PYTHON": self.py(),
            "TMPDIR": str(tmp),                       # never the Frame's /tmp (a 7.7 GB tmpfs)
            "ZIG_GLOBAL_CACHE_DIR": str(self.work / "zig-cache"),
            "ZIG_LOCAL_CACHE_DIR": str(self.work / "zig-cache"),
            "PYTHONUNBUFFERED": "1",
            "PYTHONDONTWRITEBYTECODE": "1",
            "PATH": f"{self.venv / 'bin'}:{self.zig().parent}:{os.environ.get('PATH', '/usr/bin:/bin')}",
        })
        for tool in ("strip", "objcopy", "readelf", "objdump"):
            if not os.environ.get(f"SA2_{tool.upper()}"):
                found = shutil.which(tool) or shutil.which(f"llvm-{tool}")
                if found:
                    e[f"SA2_{tool.upper()}"] = found
        return e

    def run(self, argv: list, cwd: Path | None = None, env: dict | None = None, timeout: float | None = None,
            want: str | None = None, label: str = "", stdin_null: bool = True, tree_env: bool = False) -> str:
        """Run a command, its output to the log and the status line; refuse a nonzero exit (or no
        `want` marker in its output). With tree_env, through the tree's tools/env.sh, as the
        project runs every pipeline step."""
        argv = [str(x) for x in argv]
        shown = shlex.join(argv)
        if tree_env:
            argv = ["bash", "-c", 'source tools/env.sh && exec "$@"', "bash"] + argv
        if self.dry:
            print(f"      $ {'(cd ' + str(cwd) + ') ' if cwd else ''}{shown}")
            return want or ""
        self.note(f"  $ {shown}", console=False)
        t0 = time.time()
        tail: list[str] = []
        seen_want = False
        p = subprocess.Popen(argv, cwd=str(cwd) if cwd else None, env=env or self.env(),
                             stdin=subprocess.DEVNULL if stdin_null else None,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             start_new_session=True)
        killer = None
        if timeout:
            def kill():
                try:
                    os.killpg(p.pid, 9)
                except OSError:
                    pass
            killer = threading.Timer(timeout, kill)
            killer.start()
        try:
            for line in p.stdout:
                line = line.rstrip("\n")
                self.log.write("    " + line + "\n")
                tail = (tail + [line])[-40:]
                if want and want in line:
                    seen_want = True
                self.status_line(line, t0, label)
            rc = p.wait()
        finally:
            if killer:
                killer.cancel()
        took = time.time() - t0
        self.note(f"  exit {rc} after {took:.0f} s", console=False)
        if rc != 0:
            raise StepError(f"{shown[:200]} exited {rc} after {took:.0f} s:\n  " + "\n  ".join(tail[-12:]))
        if want and not seen_want:
            raise StepError(f"{shown[:200]} printed no {want}:\n  " + "\n  ".join(tail[-12:]))
        return "\n".join(tail)

    def status_line(self, line: str, t0: float, label: str) -> None:
        n, name, title = self.current
        text = f"[{n}/{STEPS[-1][0]}] {title}"
        m = None
        for m in re.finditer(r"(\d[\d,]*)\s*/\s*(\d[\d,]*)", line):
            pass
        eta = ""
        if m:
            done, total = int(m.group(1).replace(",", "")), int(m.group(2).replace(",", ""))
            if 0 < done <= total and total > 50:
                text += f" {done:,}/{total:,}"
                el = time.time() - t0
                if done > total * 0.02 and el > 30:
                    eta = f"  ETA {max(1, round(el * (total - done) / done / 60))} min"
        text += eta
        if label:
            text += f"  ({label})"
        text += f"  | {line.strip()[:60]}"
        self.status(text)
        if time.time() - self._last_progress > 5:
            self._last_progress = time.time()
            self.progress(step=n, name=name, title=title, detail=line.strip()[:200], eta=eta.strip())

    # -- the plan
    def run_all(self) -> int:
        only = None
        if self.a.only:
            only = {step_number(x) for x in self.a.only.split(",") if x.strip()}
        start = step_number(self.a.from_step) if self.a.from_step is not None else 0
        if not self.dry:
            self.data.mkdir(parents=True, exist_ok=True)
            self.state.mkdir(parents=True, exist_ok=True)
            self.lock = open(self.state / "build.lock", "w")
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                print(f"another sa2mr_build is running ({self.state / 'build.lock'}); "
                      f"its progress: {self.state / 'progress.json'}", file=sys.stderr)
                return 3
            self.open_log()
            threading.Thread(target=self.thermal_logger, daemon=True).start()
            self.note(f"sa2mr_build: kit {self.kit}, toolchain {self.tc}, app {self.app}, data {self.data}, "
                      f"{self.jobs} jobs; log {self.log_path}")
        else:
            print(f"sa2mr_build --dry-run: kit {self.kit}, toolchain {self.tc}, app {self.app}, "
                  f"data {self.data}, {self.jobs} jobs")
        cached = False
        try:
            try:
                cached = self.kit is not None and self.kit.is_dir() and self.cache_ok()
                key = self.tkey() if self.kit and self.kit.is_dir() else "(no kit)"
            except StepError as e:
                key = f"(unknown: {e})"
            self.note(f"translation key {key}" + (": cached, steps 4-12 skipped (relink only)" if cached else ""))
            for n, name, title, fn, group in STEPS:
                forced = (only is not None and n in only) or (only is None and n >= start and
                                                               self.a.from_step is not None)
                if only is not None and n not in only:
                    continue
                if only is None and n < start:
                    continue
                if group == "translate" and cached and not forced:
                    self.note(f"[{n:2d}] {title}: cached ({self.tkey()})")
                    continue
                digest = self.step_digest(n, group)
                st = self.read_state(n, name)
                if not forced and st.get("ok") and st.get("digest") == digest and self.still_valid(n, st):
                    self.note(f"[{n:2d}] {title}: done ({time.strftime('%Y-%m-%d %H:%M', time.localtime(st.get('finished', 0)))})")
                    continue
                self.current = (n, name, title)
                if self.dry:
                    print(f"[{n:2d}] {title}: would run")
                    try:
                        fn(self)
                    except StepError as e:
                        print(f"      (dry run: {e})")
                    continue
                self.wait_cool()
                self.note(f"[{n:2d}] {title}")
                self.progress(step=n, name=name, title=title, state="running", detail="", eta="")
                t0 = time.time()
                self.write_state(n, name, {"step": n, "name": name, "digest": digest, "ok": False,
                                           "started": t0})
                data = fn(self) or {}
                self.clear_status()
                self.write_state(n, name, {"step": n, "name": name, "digest": digest, "ok": True,
                                           "started": t0, "finished": time.time(),
                                           "seconds": round(time.time() - t0, 1), "data": data})
                self.note(f"     done in {(time.time() - t0) / 60:.1f} min")
            if not self.dry:
                self.progress(state="done", step=STEPS[-1][0], name="done", title="SA2 VR is ready", detail="")
                self.note("SA2MR_BUILD_OK")
            return 0
        except StepError as e:
            self.clear_status()
            n, name, title = self.current or (-1, "?", "?")
            msg = f"step {n} ({title}) failed: {e}"
            if self.dry:
                print(msg)
                return 1
            self.note("SA2MR_BUILD_FAILED " + msg)
            self.progress(state="failed", step=n, name=name, title=title, detail=str(e)[:2000])
            self.tell_failure(n, title)
            return 1
        finally:
            self._stop.set()

    def tell_failure(self, n: int, title: str) -> None:
        if self.a.no_notify:
            return
        text = (f"Step {n} ({title}) failed. Run the installer again to resume.\nLog: {self.log_path}")
        if shutil.which("notify-send"):
            subprocess.run(["notify-send", "-a", NAME, f"{NAME}: the build failed", text],
                           stdin=subprocess.DEVNULL, capture_output=True)
        if shutil.which("kdialog") and (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            subprocess.Popen(["kdialog", "--title", NAME, "--error", text], stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    def step_digest(self, n: int, group: str) -> str:
        try:
            if group == "translate":
                return self.tkey()
            if n == 13:
                return sha256_text(self.tkey(), self.kit_digest(), str(self.app))
            if n == 1:
                return self.toolchain_digest()
            if n == 2:
                wheels = sorted(f"{w.name}:{w.stat().st_size}" for w in (self.tc / "wheels").glob("*.whl"))
                return sha256_text(sys.version, *wheels)
            if n == 3:
                inst = self.app / "bin" / "sa2-install"
                return sha256_text(sha256_file(inst) if inst.is_file() else "none")
            if n == 14:
                return sha256_text(str(self.app), NAME)
        except (StepError, OSError) as e:
            return f"unknown ({e})"
        return "always"   # preflight: every run

    def still_valid(self, n: int, st: dict) -> bool:
        if n == 0:
            return False                                  # preflight runs every time
        if 4 <= n <= 10:
            try:
                return (self.tree / ".sa2mr-key").read_text().strip() == self.tkey()
            except OSError:
                return False
        if n == 11:
            return self.cache_ok()
        if n == 13:
            game = self.app / "bin" / "sa2-game"
            return game.is_file() and sha256_file(game) == st.get("data", {}).get("sha256")
        return True


def step_number(x) -> int:
    x = str(x).strip()
    if x.isdigit():
        return int(x)
    for n, name, *_ in STEPS:
        if name == x:
            return n
    raise SystemExit(f"sa2mr_build: no step {x!r}; the steps: " + ", ".join(f"{n} {nm}" for n, nm, *_ in STEPS))


# ---------------------------------------------------------------- the steps

def s_preflight(b: Build) -> dict:
    problems = []
    arch = platform.machine()
    if arch != "aarch64" and not b.a.allow_any_arch:
        problems.append(f"this is {arch}, not aarch64 (the Steam Frame); --allow-any-arch for a test")
    if sys.version_info < (3, 10):
        problems.append(f"Python {sys.version.split()[0]} is older than 3.10")
    if b.kit is None or not (b.kit / "tools" / "s4" / "rebuild_image.py").is_file():
        problems.append(f"--kit {b.kit} is not an OpenSA2 source tree")
    elif not (b.kit / TRIMMED_MAP).is_file():
        problems.append(f"the kit has no {TRIMMED_MAP}")
    if not (b.app / "bin" / "sa2-install").is_file():
        problems.append(f"--app {b.app} has no bin/sa2-install (install the app first)")
    cached = b.kit is not None and b.kit.is_dir() and b.cache_ok()
    need = MIN_FREE_RELINK_GB if cached else MIN_FREE_GB
    probe = b.data if b.data.exists() else b.data.parent
    while not probe.exists():
        probe = probe.parent
    free = shutil.disk_usage(probe).free / 1e9
    if free < need and not b.dry:
        problems.append(f"{free:.1f} GB free under {probe}; the build needs {need} GB")
    mem = meminfo_gb("MemAvailable")
    if not cached and shutil.which("unshare") is None:
        problems.append("no unshare (remill runs in a user namespace)")
    elif not cached and not b.dry and os.environ.get("SA2_REMILL_MODE", "unshare") == "unshare":
        r = subprocess.run(["unshare", "-Urm", "true"], capture_output=True, text=True)
        if r.returncode:
            problems.append(f"unshare -Urm fails ({r.stderr.strip()[:200]}): remill's mount needs user namespaces")
    if not (os.environ.get("SA2_STRIP") or shutil.which("strip") or shutil.which("llvm-strip")):
        problems.append("no strip or llvm-strip (the game link strips its debug information; SA2_STRIP names one)")
    if not b.dry and on_battery():
        b.note("warning: the device is on battery; keep it on the charger for the build "
               "(about 1.5-3 hours the first time)")
    if problems:
        raise StepError("; ".join(problems))
    lib = sa2_installed(b.steam)
    if lib is None:
        if b.dry:
            raise StepError("SA2 (appid 213610) is not installed through Steam (a run would ask Steam to)")
        if b.a.no_wait_install:
            raise StepError("Sonic Adventure 2 (Steam app 213610) is not installed; install it in Steam first")
        b.note("Sonic Adventure 2 is not installed: asking Steam to install it (steam://install/213610); "
               "waiting for the download")
        opener = shutil.which("steam") or shutil.which("xdg-open")
        if opener:
            subprocess.Popen([opener, f"steam://install/{SA2_APPID}"], stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        t0 = time.time()
        while lib is None:
            if time.time() - t0 > 6 * 3600:
                raise StepError("SA2 was not installed within 6 hours")
            time.sleep(20)
            b.status(f"[0/14] waiting for Steam to install Sonic Adventure 2 ({(time.time() - t0) / 60:.0f} min)")
            lib = sa2_installed(b.steam)
    b.note(f"  {arch}, Python {sys.version.split()[0]}, {free:.0f} GB free, MemAvailable {mem:.1f} GB, "
           f"{os.cpu_count()} CPUs, {b.jobs} jobs; SA2 in {lib}", console=not b.dry)
    return {"library": str(lib), "jobs": b.jobs}


def toolchain_problems(tc: Path) -> list[str]:
    # include/: the runtime's remill State headers (tools/s3/whole_image/common.py REMILL_INC), which
    # the game link compiles against (tools/s4/link_s4.py), as well as the lift
    need = [tc / "remill/bin/remill-lift-14", tc / "zig/zig",
            tc / "remill/include/remill/Arch/X86/Runtime/State.h"]
    out = [f"no {p}" for p in need if not p.is_file()]
    if not list((tc / "remill/share/remill/14/semantics").glob("*.bc")):
        out.append(f"no semantics under {tc}/remill/share/remill/14/semantics")
    if not list((tc / "wheels").glob("*.whl")):
        out.append(f"no wheels under {tc}/wheels")
    return out


def s_toolchain(b: Build) -> dict:
    missing = toolchain_problems(b.tc)
    if missing:
        spec_p = b.kit / "toolchain.json" if b.kit else None
        if not spec_p or not spec_p.is_file():
            raise StepError(f"the toolchain at {b.tc} is incomplete ({'; '.join(missing)}) and the kit "
                            "names none to fetch (toolchain.json)")
        spec = json.loads(spec_p.read_text())
        dl = b.data / "downloads"
        tar = dl / Path(spec["url"]).name
        if b.dry:
            print(f"      fetch {spec['url']} -> {tar} (sha256 {spec['sha256'][:16]}...), unpack to {b.tc}")
            return {}
        dl.mkdir(parents=True, exist_ok=True)
        if not tar.is_file() or sha256_file(tar) != spec["sha256"]:
            b.run(["curl", "-fL", "--retry", "5", "-C", "-", "-o", tar, spec["url"]], label="download")
        if sha256_file(tar) != spec["sha256"]:
            tar.unlink()
            raise StepError(f"{tar.name}: sha256 differs from the kit's toolchain.json; deleted, run again")
        part = b.tc.with_name(b.tc.name + ".partial")
        shutil.rmtree(part, ignore_errors=True)
        part.mkdir(parents=True)
        b.run(["tar", "--zstd" if tar.name.endswith(".zst") else "-z", "-xf", tar, "-C", part,
               "--strip-components=1" if spec.get("strip_top", True) else "--no-same-owner"])
        shutil.rmtree(b.tc, ignore_errors=True)
        os.replace(part, b.tc)
        tar.unlink()
        missing = toolchain_problems(b.tc)
        if missing:
            raise StepError(f"the fetched toolchain is incomplete: {'; '.join(missing)}")
    manifest = b.tc / "MANIFEST.sha256"
    checked = 0
    if manifest.is_file() and not b.dry:
        bad = []
        for line in manifest.read_text().splitlines():
            if not line.strip():
                continue
            want, rel = line.split(None, 1)
            p = b.tc / rel.strip().lstrip("*")
            if not p.is_file() or sha256_file(p) != want:
                bad.append(rel.strip())
            checked += 1
        if bad:
            raise StepError(f"the toolchain differs from its MANIFEST.sha256: {bad[:5]} ({len(bad)} files)")
    elif not b.dry:
        b.note("  the toolchain has no MANIFEST.sha256 (a local toolchain): its layout only")
    if not b.dry:
        b.run([b.zig(), "version"], label="zig")
    return {"manifest_files": checked}


def s_venv(b: Build) -> dict:
    wheels = sorted((b.tc / "wheels").glob("*.whl"))
    if not b.venv.joinpath("bin", "python").exists():
        b.run([sys.executable, "-m", "venv", "--clear", b.venv], label="venv")
    names = sorted({w.name.split("-")[0].replace("_", "-") for w in wheels})
    b.run([b.py(), "-m", "pip", "install", "--no-index", "--disable-pip-version-check", "--find-links",
           b.tc / "wheels", *names], label="pip")
    b.run([b.py(), "-c", "import pefile, iced_x86; print('VENV_OK', pefile.__version__)"], want="VENV_OK")
    return {"wheels": [w.name for w in wheels]}


def s_install(b: Build) -> dict:
    b.run([b.app / "bin" / "sa2-install", "--source", "steam", "--yes"], cwd=b.app, want="SA2_INSTALL_OK",
          label="verifying the game files")
    if b.dry:
        return {}
    manifest, root = install_manifest_root()
    exe = root / "sonic2app.exe"
    if not exe.is_file():
        raise StepError(f"{manifest} names {root}, which has no sonic2app.exe")
    have = sha256_file(exe)
    if have != SHIPPED_SHA256:
        raise StepError(f"{exe} is sha256 {have[:16]}..., not the Steam build this needs ({SHIPPED_SHA256[:16]}...)")
    b.note(f"  the game folder: {root} (sonic2app.exe {have[:8]}...)")
    return {"game_dir": str(root), "manifest": str(manifest)}


def game_dir(b: Build) -> Path:
    d = b.read_state(3, "install").get("data", {}).get("game_dir")
    if not d:
        if b.dry:
            return Path("<the game folder step 3 finds>")
        raise StepError("step 3 (install) has not recorded the game folder; run from step 3")
    return Path(d)


def s_worktree(b: Build) -> dict:
    exe = game_dir(b) / "sonic2app.exe"
    if b.dry:
        print(f"      copy {b.kit} -> {b.tree}; {exe} -> private/exe/sonic2app.exe; .venv -> {b.venv}")
        return {}
    marker = b.tree / ".sa2mr-key"
    if b.tree.exists():
        b.note(f"  removing the old work tree {b.tree}")
        shutil.rmtree(b.tree)
    b.clear_states(range(5, 13))
    b.work.mkdir(parents=True, exist_ok=True)
    shutil.copytree(b.kit, b.tree, symlinks=True)
    (b.tree / "private" / "exe").mkdir(parents=True, exist_ok=True)
    (b.tree / "private" / "generated").mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, b.tree / "private" / "exe" / "sonic2app.exe")
    if sha256_file(b.tree / "private" / "exe" / "sonic2app.exe") != SHIPPED_SHA256:
        raise StepError("the copied sonic2app.exe does not hash to the Steam build's sha256")
    venv_link = b.tree / ".venv"
    if venv_link.is_symlink() or venv_link.exists():
        venv_link.unlink()
    venv_link.symlink_to(b.venv)
    marker.write_text(b.tkey() + "\n")
    return {"tree": str(b.tree)}


def fetch(b: Build, url: str, sha: str, dst: Path) -> None:
    if dst.is_file() and sha256_file(dst) == sha:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(dst.suffix + ".part")
    b.note(f"  downloading {url}")
    if shutil.which("curl"):
        b.run(["curl", "-fL", "--retry", "5", "-o", tmp, url], label="download")
    else:
        with urllib.request.urlopen(url, timeout=120) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
    have = sha256_file(tmp)
    if have != sha:
        tmp.unlink()
        raise StepError(f"{url}: sha256 {have[:16]}..., not {sha[:16]}...")
    os.replace(tmp, dst)


def s_unwrap(b: Build) -> dict:
    unpacked = b.tree / "private" / "exe" / "sonic2app-unpacked.exe"
    zip_p = b.data / "downloads" / "Steamless.v3.1.0.5.zip"
    sl = b.work / "steamless"
    proton = find_tool_dir(b.steam, PROTON_NAMES, b.a.proton, "--proton")
    slr = find_tool_dir(b.steam, SLR_NAMES, b.a.slr, "--slr") if (SLR_NAMES or b.a.slr) else None
    if proton is None:
        raise StepError(f"no {' or '.join(PROTON_NAMES)} in Steam's libraries: install it in Steam "
                        "(Library, Tools) and run again")
    if SLR_NAMES and slr is None:
        raise StepError(f"no {SLR_NAMES[0]} in Steam's libraries: start any Proton game once, or install it "
                        "from Library, Tools, and run again")
    run_exe = sl / "run" / "sonic2app.exe"
    cmd = ([slr / "run", "--"] if slr else []) + [proton / "proton", "runinprefix", "Steamless.CLI.exe",
                                                  "Z:" + str(run_exe).replace("/", "\\")]
    prefix = b.work / "proton-prefix"
    env = dict(b.env(), STEAM_COMPAT_DATA_PATH=str(prefix),
               STEAM_COMPAT_CLIENT_INSTALL_PATH=str(b.steam))
    if b.dry:
        print(f"      fetch {STEAMLESS_URL} (sha256 {STEAMLESS_SHA256[:16]}...) -> {zip_p}; unzip -> {sl}/cli")
        b.run(cmd, cwd=sl / "cli")
        b.run([b.py(), "tools/normalize_unpacked.py", "private/exe/sonic2app-unpacked.exe"], cwd=b.tree)
        b.run([b.py(), "sa2mr/map/restore_map.py", "--in", TRIMMED_MAP], cwd=b.tree, want="RESTORE_MAP_OK")
        return {}
    if not (unpacked.is_file() and sha256_file(unpacked) == UNPACKED_SHA256):
        fetch(b, STEAMLESS_URL, STEAMLESS_SHA256, zip_p)
        shutil.rmtree(sl, ignore_errors=True)
        (sl / "cli").mkdir(parents=True)
        with zipfile.ZipFile(zip_p) as z:
            z.extractall(sl / "cli")
        clis = sorted((sl / "cli").rglob("Steamless.CLI.exe"))
        if not clis:
            raise StepError("the Steamless zip holds no Steamless.CLI.exe")
        cli_dir = clis[0].parent
        for dll in STEAMLESS_PLUGINS:
            src = cli_dir / "Plugins" / dll
            if not src.is_file():
                raise StepError(f"the Steamless zip has no Plugins/{dll}")
            shutil.copy2(src, cli_dir / dll)
        run_exe.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(game_dir(b) / "sonic2app.exe", run_exe)
        prefix.mkdir(parents=True, exist_ok=True)
        out = run_exe.with_name(run_exe.name + ".unpacked.exe")
        if out.exists():
            out.unlink()
        b.note(f"  Steamless under {proton.name}" + (f" in {slr.name}" if slr else ""))
        b.run(cmd, cwd=cli_dir, env=env, timeout=STEAMLESS_TIMEOUT, label="Steamless under Proton")
        if not out.is_file():
            raise StepError(f"Steamless wrote no {out.name} (see the log)")
        os.replace(out, unpacked)
        if sha256_file(unpacked) != UNPACKED_SHA256:
            b.run([b.py(), "tools/normalize_unpacked.py", "private/exe/sonic2app-unpacked.exe"], cwd=b.tree)
        have = sha256_file(unpacked)
        if have != UNPACKED_SHA256:
            raise StepError(f"the unpacked executable is sha256 {have[:16]}..., not the pinned "
                            f"{UNPACKED_SHA256[:16]}...")
        shutil.rmtree(sl / "run", ignore_errors=True)
    b.run([b.py(), "sa2mr/map/restore_map.py", "--in", TRIMMED_MAP], cwd=b.tree, want="RESTORE_MAP_OK")
    return {"unpacked_sha256": UNPACKED_SHA256, "proton": str(proton), "slr": str(slr) if slr else None}


def s_translate(b: Build) -> dict:
    b.run([".venv/bin/python", "tools/s4/rebuild_image.py", "--jobs", b.jobs], cwd=b.tree, tree_env=True,
          label=f"{b.jobs} jobs")
    return {"jobs": b.jobs}


def s_receipt(b: Build) -> dict:
    out = b.run([".venv/bin/python", "tools/s4/lift_ir_receipt.py"], cwd=b.tree, tree_env=True, want="LIFT_IR_OK")
    line = next((l for l in out.splitlines() if "LIFT_IR_OK" in l), "")
    return {"receipt": line.strip()}


def s_link_inputs(b: Build) -> dict:
    py = ".venv/bin/python"
    b.run([py, "tools/s4/mb_edges.py", "--tag", TAG, "--lift-tag", "next", "--arms-tag", "next"],
          cwd=b.tree, tree_env=True, label="edges")
    b.run([py, "tools/s3/whole_image/gen_table.py", "--tag", TAG, "--out",
           f"private/generated/s3/link/{TAG}/function_table_whole.c"], cwd=b.tree, tree_env=True, label="table")
    b.run([py, "tools/s7/drm_unwrap.py", "--companion"], cwd=b.tree, tree_env=True, label="DRM companion")
    b.run([py, "sa2mr/finalize_extras.py"], cwd=b.tree, tree_env=True, label="extras")
    return {}


def s_aarch64(b: Build) -> dict:
    b.run([".venv/bin/python", "sa2mr/arm64/lift_arm64.py", "--target", "aarch64-linux-musl", "--mode", "double",
           "--out", F80_OUT, "--jobs", b.jobs], cwd=b.tree, tree_env=True, label=f"{b.jobs} jobs")
    return {}


def s_tables(b: Build) -> dict:
    b.run([".venv/bin/python", "sa2mr/arm64/release_build_arm64.py", "--only", "tables"], cwd=b.tree,
          tree_env=True, want="RELEASE_BUILD_ARM64_OK")
    return {}


def place_tree(src: Path, dst: Path) -> int:
    """Hard links of every file under `src` at `dst` (a copy across file systems); bytes placed."""
    total = 0
    for p in sorted(src.rglob("*")):
        q = dst / p.relative_to(src)
        if p.is_symlink():
            q.parent.mkdir(parents=True, exist_ok=True)
            q.symlink_to(os.readlink(p))
        elif p.is_dir():
            q.mkdir(parents=True, exist_ok=True)
        elif p.is_file():
            q.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(p, q)
            except OSError:
                shutil.copy2(p, q)
            total += p.stat().st_size
    return total


def s_cache(b: Build) -> dict:
    dst = b.cache_dir()
    if b.dry:
        print(f"      keep {', '.join(CACHE_KEEP)} -> {dst}/files")
        return {}
    part = dst.with_name(dst.name + ".partial")
    shutil.rmtree(part, ignore_errors=True)
    total = 0
    for rel in CACHE_KEEP:
        src = b.tree / rel
        if not src.exists():
            raise StepError(f"the work tree has no {rel} to keep")
        total += place_tree(src, part / "files" / rel)
    (part / "cache.json").write_text(json.dumps({"key": b.tkey(), "created": time.time(), "bytes": total,
                                                 "keep": list(CACHE_KEEP)}, indent=1) + "\n")
    shutil.rmtree(dst, ignore_errors=True)
    os.replace(part, dst)
    b.note(f"  cached {total / 1e6:.0f} MB in {dst}")
    return {"bytes": total, "dir": str(dst)}


def s_cleanup(b: Build) -> dict:
    if b.dry:
        print(f"      rm -rf {b.work}")
        return {}
    if b.work.exists():
        shutil.rmtree(b.work)
    return {}


def s_link(b: Build) -> dict:
    link = b.work / "link" / "opensa2"
    if b.dry:
        print(f"      link tree {link}: the kit + {b.cache_dir()}/files")
        b.run([".venv/bin/python", "sa2mr/arm64/release_build_arm64.py", "--only", "game", "--f80-cached"],
              cwd=link, tree_env=True, want="RELEASE_BUILD_ARM64_OK")
        print(f"      install {RELEASE_OUT}/bin/sa2-game -> {b.app}/bin/sa2-game")
        return {}
    if not b.cache_ok():
        raise StepError(f"no complete cache for the translation key {b.tkey()}: run from step 4")
    shutil.rmtree(link.parent, ignore_errors=True)
    link.parent.mkdir(parents=True)
    shutil.copytree(b.kit, link, symlinks=True)
    place_tree(b.cache_dir() / "files", link)
    (link / ".venv").symlink_to(b.venv)
    b.run([".venv/bin/python", "sa2mr/arm64/release_build_arm64.py", "--only", "game", "--f80-cached"],
          cwd=link, tree_env=True, want="RELEASE_BUILD_ARM64_OK", label="linking the game")
    built = link / RELEASE_OUT / "bin" / "sa2-game"
    if not built.is_file():
        raise StepError(f"the link wrote no {built}")
    (b.app / "bin").mkdir(parents=True, exist_ok=True)
    tmp = b.app / "bin" / ".sa2-game.tmp"
    shutil.copy2(built, tmp)
    os.chmod(tmp, 0o755)
    os.replace(tmp, b.app / "bin" / "sa2-game")
    needs = link / RELEASE_OUT / "share" / "cpu-needs.txt"
    if needs.is_file():
        (b.app / "share").mkdir(parents=True, exist_ok=True)
        shutil.copy2(needs, b.app / "share" / "cpu-needs.txt")
    sha = sha256_file(b.app / "bin" / "sa2-game")
    (b.app / "share" / "sa2-game.build.json").write_text(json.dumps(
        {"translation_key": b.tkey(), "sha256": sha, "linked": time.strftime("%Y-%m-%dT%H:%M:%S%z")},
        indent=1) + "\n")
    shutil.rmtree(b.work, ignore_errors=True)
    # The new game links and is in place: older translations' caches go.
    for old in b.cache.iterdir():
        if old.name != b.tkey():
            shutil.rmtree(old, ignore_errors=True)
    b.note(f"  bin/sa2-game {sha[:16]}... ({(b.app / 'bin' / 'sa2-game').stat().st_size / 1e6:.0f} MB)")
    return {"sha256": sha}


def s_steam(b: Build) -> dict:
    launch = b.app / "sa2mr-vr.sh"
    for name, dst, mode in (("sa2mr-vr.sh", launch, 0o755), ("game.env", b.app / "share" / "game.env.vr", 0o644)):
        src = HERE / name
        if src.is_file() and (not dst.is_file() or dst.read_bytes() != src.read_bytes()):
            if b.dry:
                print(f"      install {src} -> {dst}")
                continue
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            os.chmod(dst, mode)
    if b.a.no_steam:
        b.note("  --no-steam: no Steam entry")
        return {}
    cmd = [sys.executable, HERE / "add_to_steam.py", "--app", b.app, "--exe", launch,
           "--steam-root", b.steam] + ([] if b.a.no_stop_steam else ["--stop-steam"])
    out = b.run(cmd, label="the Steam entry", want="SA2MR_STEAM_ENTRY_OK")
    if not b.dry and not b.a.no_notify and shutil.which("notify-send"):
        subprocess.run(["notify-send", "-a", NAME, f"{NAME} is ready",
                        "Find it in your Steam library and start it in VR."],
                       stdin=subprocess.DEVNULL, capture_output=True)
    return {"entry": out.splitlines()[-1] if out else ""}


STEPS = (
    (0, "preflight", "Checking the device", s_preflight, "setup"),
    (1, "toolchain", "Checking the toolchain", s_toolchain, "setup"),
    (2, "venv", "Python environment", s_venv, "setup"),
    (3, "install", "Verifying the game files", s_install, "setup"),
    (4, "worktree", "Preparing the work tree", s_worktree, "translate"),
    (5, "unwrap", "Removing the DRM wrapper (Steamless)", s_unwrap, "translate"),
    (6, "translate", "Translating", s_translate, "translate"),
    (7, "receipt", "Checking the translation (LIFT_IR_OK)", s_receipt, "translate"),
    (8, "link-inputs", "Edge table, function table, extras", s_link_inputs, "translate"),
    (9, "aarch64", "Compiling for aarch64", s_aarch64, "translate"),
    (10, "tables", "Install and overlay tables", s_tables, "translate"),
    (11, "cache", "Keeping the build cache", s_cache, "translate"),
    (12, "cleanup", "Deleting the work files", s_cleanup, "translate"),
    (13, "link", "Linking the game", s_link, "link"),
    (14, "steam", "Adding SA2 VR to Steam", s_steam, "finish"),
)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    home = Path.home()
    ap.add_argument("--kit", help="the unpacked OpenSA2 source tree (module docstring)")
    ap.add_argument("--toolchain", default=str(home / ".local/share/sa2mr/toolchain"))
    ap.add_argument("--app", default=str(home / "Applications/sa2mr"))
    ap.add_argument("--data", default=str(home / ".local/share/sa2mr"))
    ap.add_argument("--steam-root", default=str(home / ".local/share/Steam"))
    ap.add_argument("--jobs", type=int, default=0, help="parallel jobs (default: from CPUs and memory)")
    ap.add_argument("--from-step", help="rerun from this step (number or name), ignoring its state")
    ap.add_argument("--only", help="run only these steps (comma-separated numbers or names)")
    ap.add_argument("--dry-run", action="store_true", help="print the plan and the commands; change nothing")
    ap.add_argument("--proton", help="Proton's directory (default: Proton 11.0 (ARM64) in Steam's libraries)")
    ap.add_argument("--slr", help="the Steam Linux Runtime's directory (default: SteamLinuxRuntime_4-arm64)")
    ap.add_argument("--no-steam", action="store_true", help="no Steam entry (step 14 installs the launch script only)")
    ap.add_argument("--no-stop-steam", action="store_true",
                    help="write the Steam entry without stopping Steam (it may overwrite the file)")
    ap.add_argument("--no-notify", action="store_true", help="no notify-send or kdialog")
    ap.add_argument("--no-wait-install", action="store_true", help="fail instead of asking Steam to install SA2")
    ap.add_argument("--allow-any-arch", action="store_true", help="run on a machine that is not aarch64 (tests)")
    a = ap.parse_args(argv)
    if a.from_step is not None and a.only:
        raise SystemExit("sa2mr_build: --from-step or --only, not both")
    if a.kit is None:
        default_kit = Path(a.app).expanduser() / "kit"
        a.kit = str(default_kit) if default_kit.is_dir() else None
    return Build(a).run_all()


if __name__ == "__main__":
    sys.exit(main())
