#!/usr/bin/env python3
"""sa2mr_scan: refuse anything proprietary before an upload (Actions artifact, cache, Release asset).

    python3 tools/release/sa2mr_scan.py [--profile app|toolchain|kit] PATH...

PATH is a directory, a file, or an archive (.tar, .tar.gz/.tgz, .tar.zst, .tar.xz, .zip, .whl);
archives are read member by member, nested archives too (depth 3). Prints one line per refusal
and exits 1, or prints SCAN_OK with the counts and exits 0.

What is refused (the user's hard rule: no SEGA code or data, nothing translated or generated from
it, no game screenshots, anywhere on GitHub):
  - PE images (MZ with e_lfanew -> "PE\\0\\0"), and the suffixes .exe .dll .sys .lib .pdb;
  - a file named sa2-game (the translated game), and build output: .ll .o .obj .a;
  - .bc unless its sha256 is allowlisted (remill's semantics bitcode, pinned in
    build-env/remill-arm64/sources.lock; --bc-allowlist adds files of "<sha256> <name>" lines);
  - any ELF with more than 50 distinct names matching sub_[0-9a-f]{6,8} (lifted functions), counted
    in its symbol tables and in its raw bytes, and any ELF that is not AArch64;
  - files whose sha256 is a game file's (OpenSA2's Documentation/receipts/install-hashes-v1.json)
    or a pinned executable's (tools/pins.json "executables");
  - images, audio and video, and the game's own data formats (by suffix and by magic);
  - anything under a private/ or .git/ directory, absolute or escaping symlinks, device files;
  - a binary file of no allowed type (allowed: text, AArch64 ELF, the allowlisted .bc, and in the
    toolchain profile Python wheels), and files over the size caps.
Profiles: app (the installer's tarball: ELFs only under bin/ and lib/), toolchain (the remill/zig
asset: wheels, and zig's .der certificates and abilists files allowed), kit (the OpenSA2 source: no ELF at all).
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
OPENSA2 = ROOT / "external" / "OpenSA2"
SOURCES_LOCK = ROOT / "build-env" / "remill-arm64" / "sources.lock"
SUB_RE = re.compile(rb"sub_[0-9a-f]{6,8}(?![0-9a-zA-Z_])")
SUB_NAME = re.compile(r"^sub_[0-9a-f]{6,8}$")
SUB_LIMIT = 50
BAD_NAMES = {"sa2-game"}
BUILD_SUFFIXES = {".ll", ".o", ".obj", ".a"}
PE_SUFFIXES = {".exe", ".dll", ".sys", ".lib", ".pdb", ".scr", ".cpl", ".ocx"}
MEDIA_SUFFIXES = set((".png .jpg .jpeg .gif .bmp .webp .tga .tif .tiff .ico .dds .ktx .heic .avif "
                      ".wav .ogg .mp3 .flac .opus .adx .ahx .csb .sfd .mp4 .mkv .webm .avi .mov .y4m "
                      ".prs .pak .gvr .gvm .pvr .pvm .afs .sa2s .mdl .bin.prs .sav").split())
MEDIA_MAGIC = ((b"\x89PNG\r\n\x1a\n", "a PNG image"), (b"\xff\xd8\xff", "a JPEG image"),
               (b"GIF87a", "a GIF image"), (b"GIF89a", "a GIF image"), (b"DDS ", "a DDS texture"),
               (b"OggS", "Ogg audio/video"), (b"fLaC", "FLAC audio"), (b"\x1aE\xdf\xa3", "a Matroska/WebM video"),
               (b"GVRT", "a GVR texture"), (b"PVRT", "a PVR texture"), (b"GCIX", "a GVR texture"),
               (b"GBIX", "a PVR/GVR texture"), (b"YUV4MPEG2", "a Y4M video"), (b"AFS\0", "an AFS archive"))
ARCHIVE_SUFFIXES = (".tar", ".tar.gz", ".tgz", ".tar.zst", ".tar.xz", ".zip", ".whl")
EM_AARCH64 = 183
PROFILES = {
    # max bytes per file, max total bytes, ELF allowed where, extra binary suffixes allowed
    "app": dict(file_cap=120 << 20, total_cap=600 << 20, elf_dirs=("bin", "lib"), binary_ok=(), wheels=False),
    "toolchain": dict(file_cap=200 << 20, total_cap=1200 << 20, elf_dirs=None, binary_ok=(".der", "abilists"), wheels=True),
    "kit": dict(file_cap=20 << 20, total_cap=200 << 20, elf_dirs=(), binary_ok=(), wheels=False),
}


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def load_forbidden(opensa2: Path, extra: list[Path]) -> dict[str, str]:
    """sha256 -> what it is: every game file the installer verifies, and the pinned executables."""
    out = {}
    rec = opensa2 / "Documentation" / "receipts" / "install-hashes-v1.json"
    pins = opensa2 / "tools" / "pins.json"
    for p in ([rec] if rec.is_file() or not extra else []) + list(extra):
        if not p.is_file():
            raise SystemExit(f"sa2mr_scan: no {p} (the game files' hashes); pass --opensa2 or --forbidden-hashes")
        data = json.loads(p.read_text())
        for f in data.get("files", []):
            if f.get("sha256"):
                out[f["sha256"].lower()] = f"the game file {f.get('path', '?')}"
    if pins.is_file():
        for name, e in json.loads(pins.read_text()).get("executables", {}).items():
            if e.get("sha256"):
                out[e["sha256"].lower()] = f"the executable {name} (tools/pins.json)"
    if len(out) < 100 and not extra:
        raise SystemExit(f"sa2mr_scan: only {len(out)} forbidden hashes loaded from {rec}: refusing to scan blind")
    return out


def load_bc_allowlist(lock: Path, extra: list[Path]) -> dict[str, str]:
    out = {}
    if lock.is_file():
        text = lock.read_text()
        m = re.search(r"SEMANTICS='(.*?)'", text, re.S)
        for line in (m.group(1) if m else "").splitlines():
            parts = line.split()
            if len(parts) == 2 and re.fullmatch(r"[0-9a-f]{64}", parts[0]):
                out[parts[0]] = parts[1]
    for p in extra:
        for line in p.read_text().splitlines():
            parts = line.split()
            if parts and re.fullmatch(r"[0-9a-f]{64}", parts[0]):
                out[parts[0]] = parts[1] if len(parts) > 1 else p.name
    return out


# ---------------------------------------------------------------- ELF

def elf_symbol_names(b: bytes) -> tuple[int, set[str]]:
    """(e_machine, the names in .symtab and .dynsym); a malformed ELF raises ValueError."""
    if len(b) < 52 or b[:4] != b"\x7fELF":
        raise ValueError("not an ELF")
    cls, data = b[4], b[5]
    end = "<" if data == 1 else ">"
    if cls == 2:
        machine, = struct.unpack_from(end + "H", b, 18)
        shoff, = struct.unpack_from(end + "Q", b, 40)
        shentsize, shnum = struct.unpack_from(end + "HH", b, 58)
        shfmt, symfmt, symsize = end + "IIQQQQIIQQ", end + "IBBHQQ", 24
    elif cls == 1:
        machine, = struct.unpack_from(end + "H", b, 18)
        shoff, = struct.unpack_from(end + "I", b, 32)
        shentsize, shnum = struct.unpack_from(end + "HH", b, 46)
        shfmt, symfmt, symsize = end + "IIIIIIIIII", end + "IIIBBH", 16
    else:
        raise ValueError("bad ELF class")
    names: set[str] = set()
    if not shoff or not shnum:
        return machine, names
    if shoff + shnum * shentsize > len(b):
        raise ValueError("section headers out of range")
    secs = [struct.unpack_from(shfmt, b, shoff + i * shentsize) for i in range(shnum)]
    for s in secs:
        stype = s[1]
        if stype not in (2, 11):          # SHT_SYMTAB, SHT_DYNSYM
            continue
        off, size, link = s[4], s[5], s[6]
        if link >= len(secs):
            continue
        stroff, strsize = secs[link][4], secs[link][5]
        strtab = b[stroff:stroff + strsize]
        for i in range(0, min(size, len(b) - off) // symsize):
            ent = struct.unpack_from(symfmt, b, off + i * symsize)
            name_off = ent[0]
            if name_off and name_off < len(strtab):
                e = strtab.find(b"\0", name_off)
                names.add(strtab[name_off:e if e >= 0 else len(strtab)].decode("latin-1"))
    return machine, names


def is_pe(b: bytes) -> bool:
    if len(b) < 64 or b[:2] != b"MZ":
        return False
    off, = struct.unpack_from("<I", b, 60)
    return off + 4 <= len(b) and b[off:off + 4] == b"PE\0\0"


def is_text(b: bytes) -> bool:
    if b"\0" in b:
        return False
    try:
        b.decode("utf-8")
        return True
    except UnicodeDecodeError:
        pass
    # mostly printable latin-1 (licence files, old sources)
    sample = b[:65536]
    printable = sum(1 for c in sample if c in (9, 10, 12, 13) or 32 <= c < 127 or c >= 160)
    return printable >= 0.97 * max(1, len(sample))


# ---------------------------------------------------------------- the scan

class Scan:
    def __init__(self, profile: str, forbidden: dict[str, str], bc_ok: dict[str, str]):
        self.p = PROFILES[profile]
        self.profile = profile
        self.forbidden = forbidden
        self.bc_ok = bc_ok
        self.problems: list[str] = []
        self.files = 0
        self.total = 0
        self.kinds: dict[str, int] = {}

    def bad(self, where: str, why: str) -> None:
        self.problems.append(f"{where}: {why}")

    def kind(self, k: str) -> None:
        self.kinds[k] = self.kinds.get(k, 0) + 1

    def check_path(self, where: str, rel: str) -> bool:
        parts = PurePosixPath(rel).parts
        if self.profile == "toolchain" and "zig/lib/" in rel:
            parts = tuple(p for p in parts if p != "private")   # zig's libc headers (wasi headers/private)
        if any(p in ("private", ".git") for p in parts[:-1]) or parts[-1:] == (".git",):
            self.bad(where, "under a private/ or .git/ directory")
            return False
        return True

    def check_link(self, where: str, rel: str, target: str) -> None:
        if target.startswith("/"):
            self.bad(where, f"absolute symlink -> {target}")
            return
        depth = 0
        for part in list(PurePosixPath(rel).parent.parts) + list(PurePosixPath(target).parts):
            depth += -1 if part == ".." else (0 if part == "." else 1)
            if depth < 0:
                self.bad(where, f"symlink out of the tree -> {target}")
                return

    def check_file(self, where: str, rel: str, data: bytes, depth: int = 0) -> None:
        self.files += 1
        self.total += len(data)
        if not self.check_path(where, rel):
            return
        name = PurePosixPath(rel).name
        low = name.lower()
        suffix = PurePosixPath(low).suffix
        top = PurePosixPath(rel).parts
        if len(data) > self.p["file_cap"]:
            self.bad(where, f"{len(data)} bytes, over the {self.p['file_cap'] >> 20} MB cap per file")
        h = sha256(data)
        if h in self.forbidden:
            self.bad(where, f"sha256 {h[:16]}... is {self.forbidden[h]}")
            return
        if name in BAD_NAMES:
            self.bad(where, f"named {name} (the translated game is never shipped)")
            return
        if suffix in BUILD_SUFFIXES:
            self.bad(where, f"build output ({suffix})")
            return
        if suffix in PE_SUFFIXES:
            self.bad(where, f"a Windows executable suffix ({suffix})")
            return
        if is_pe(data):
            self.bad(where, "a PE image (MZ ... PE\\0\\0)")
            return
        if suffix in MEDIA_SUFFIXES or any(low.endswith(s) for s in MEDIA_SUFFIXES if s.count(".") > 1):
            self.bad(where, f"an image, audio, video or game-data suffix ({suffix})")
            return
        for magic, what in MEDIA_MAGIC:
            if data.startswith(magic):
                self.bad(where, what)
                return
        if data[:4] == b"RIFF" and data[8:12] in (b"WAVE", b"AVI ", b"WEBP"):
            self.bad(where, f"a RIFF {data[8:12].decode().strip()} file")
            return
        if suffix == ".bc" or data[:4] in (b"BC\xc0\xde", b"\xde\xc0\x17\x0b"):
            if h in self.bc_ok:
                self.kind("bitcode (allowlisted)")
            else:
                self.bad(where, f"LLVM bitcode not on the allowlist (sha256 {h[:16]}...)")
            return
        if data[:4] == b"\x7fELF":
            self.check_elf(where, top, data)
            return
        if depth < 3 and (low.endswith(ARCHIVE_SUFFIXES) or data[:4] == b"PK\x03\x04"
                          or data[:2] == b"\x1f\x8b" or data[:4] == b"\x28\xb5\x2f\xfd"):
            if low.endswith(".whl") and not self.p["wheels"]:
                self.bad(where, "a Python wheel (only the toolchain ships wheels)")
                return
            self.kind("archive")
            self.scan_archive(where, data, depth + 1)
            return
        if is_text(data):
            self.kind("text")
            return
        if suffix in self.p["binary_ok"] or name in self.p["binary_ok"]:
            self.kind(f"binary {suffix or name}")
            return
        self.bad(where, "a binary file of no allowed type")

    def check_elf(self, where: str, top: tuple, data: bytes) -> None:
        dirs = self.p["elf_dirs"]
        if dirs is not None and (not top or top[0] not in dirs) and not (len(top) > 1 and top[1] in dirs):
            self.bad(where, "an ELF outside " + ("/".join(dirs) if dirs else "(none allowed in this profile)"))
            return
        try:
            machine, names = elf_symbol_names(data)
        except (ValueError, struct.error) as e:
            self.bad(where, f"a malformed ELF ({e})")
            return
        if machine != EM_AARCH64:
            self.bad(where, f"an ELF for machine {machine}, not AArch64")
        subs = {n for n in names if SUB_NAME.match(n)}
        raw = set(SUB_RE.findall(data))
        if len(subs) > SUB_LIMIT:
            self.bad(where, f"defines {len(subs)} lifted-function symbols (sub_<hex>; limit {SUB_LIMIT})")
        elif len(raw) > SUB_LIMIT:
            self.bad(where, f"holds {len(raw)} distinct sub_<hex> names (lifted functions; limit {SUB_LIMIT})")
        self.kind("ELF aarch64")

    # -- containers
    def scan_archive(self, where: str, data: bytes, depth: int) -> None:
        if data[:4] == b"PK\x03\x04":
            try:
                z = zipfile.ZipFile(io.BytesIO(data))
            except zipfile.BadZipFile as e:
                self.bad(where, f"a damaged zip ({e})")
                return
            for info in z.infolist():
                if info.is_dir():
                    continue
                mode = (info.external_attr >> 16) & 0o170000
                if mode == stat.S_IFLNK:
                    self.check_link(f"{where}!{info.filename}", info.filename, z.read(info).decode("utf-8", "replace"))
                    continue
                self.check_file(f"{where}!{info.filename}", info.filename, z.read(info), depth)
            return
        if data[:4] == b"\x28\xb5\x2f\xfd":
            data = zstd_decompress(data, where)
            if data is None:
                self.bad(where, "a zstd stream, and no zstd here to read it")
                return
        try:
            t = tarfile.open(fileobj=io.BytesIO(data), mode="r:*")
        except tarfile.TarError as e:
            self.bad(where, f"a compressed file that is not a tar archive ({e})")
            return
        with t:
            self.scan_tar(where, t, depth)

    def scan_tar(self, where: str, t: tarfile.TarFile, depth: int) -> None:
        for m in t:
            w = f"{where}!{m.name}"
            if m.isdir():
                self.check_path(w, m.name + "/x")
                continue
            if m.issym():
                self.check_link(w, m.name, m.linkname)
                continue
            if m.islnk():
                continue                       # the target is a member of its own
            if not m.isfile():
                self.bad(w, "a device, fifo or other special file")
                continue
            f = t.extractfile(m)
            self.check_file(w, m.name, f.read() if f else b"", depth)

    def scan_path(self, p: Path) -> None:
        if p.is_dir():
            for d, dirs, files in os.walk(p, followlinks=False):
                dirs.sort()
                for n in sorted(files) + sorted(x for x in dirs if os.path.islink(os.path.join(d, x))):
                    full = Path(d) / n
                    rel = full.relative_to(p).as_posix()
                    if full.is_symlink():
                        self.check_link(rel, rel, os.readlink(full))
                        continue
                    st = full.lstat()
                    if not stat.S_ISREG(st.st_mode):
                        self.bad(rel, "a device, fifo or other special file")
                        continue
                    self.check_file(rel, rel, full.read_bytes())
            return
        data = p.read_bytes()
        low = p.name.lower()
        if low.endswith(ARCHIVE_SUFFIXES):
            if low.endswith(".whl") and not self.p["wheels"]:
                self.bad(p.name, "a Python wheel (only the toolchain ships wheels)")
                return
            self.files += 0
            self.scan_archive(p.name, data, 1)
        else:
            self.check_file(p.name, p.name, data)


def zstd_decompress(data: bytes, where: str) -> bytes | None:
    exe = shutil.which("zstd")
    if not exe:
        return None
    r = subprocess.run([exe, "-dc"], input=data, capture_output=True)
    if r.returncode:
        raise SystemExit(f"sa2mr_scan: zstd could not read {where}: {r.stderr.decode()[:200]}")
    return r.stdout


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--profile", choices=sorted(PROFILES), default="app")
    ap.add_argument("--opensa2", type=Path, default=OPENSA2, help="the OpenSA2 checkout (its receipts and pins)")
    ap.add_argument("--forbidden-hashes", type=Path, action="append", default=[],
                    help="more JSON files of {files: [{path, sha256}]} to refuse")
    ap.add_argument("--bc-allowlist", type=Path, action="append", default=[],
                    help="more '<sha256> <name>' lines of allowed .bc files")
    ap.add_argument("--sources-lock", type=Path, default=SOURCES_LOCK)
    a = ap.parse_args(argv)
    forbidden = load_forbidden(a.opensa2, a.forbidden_hashes)
    bc_ok = load_bc_allowlist(a.sources_lock, a.bc_allowlist)
    s = Scan(a.profile, forbidden, bc_ok)
    for p in a.paths:
        if not p.exists():
            raise SystemExit(f"sa2mr_scan: no {p}")
        s.scan_path(p)
    if s.total > s.p["total_cap"]:
        s.bad("(all)", f"{s.total} bytes, over the {s.p['total_cap'] >> 20} MB cap of the {a.profile} profile")
    for line in s.problems:
        print(f"REFUSED {line}")
    kinds = ", ".join(f"{k} {v}" for k, v in sorted(s.kinds.items()))
    if s.problems:
        print(f"SCAN_FAILED {len(s.problems)} problem(s) in {s.files} files ({a.profile} profile)")
        return 1
    print(f"SCAN_OK {s.files} files, {s.total / 1e6:.1f} MB ({a.profile} profile; {kinds}; "
          f"{len(forbidden)} forbidden hashes, {len(bc_ok)} allowlisted .bc)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
