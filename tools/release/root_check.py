#!/usr/bin/env python3
"""root_check: what the root repo (github.com/jonnyczi/sa2mr) tracks holds nothing proprietary.

    python3 tools/release/root_check.py [--repo DIR]

Reads the index (`git ls-files -s`, the blobs through `git cat-file --batch`), so it checks what a
commit would hold, not the working tree. Refuses:
  - anything under media/ (game screenshots) or a private/ directory;
  - anything under external/ but the OpenSA2 submodule (a gitlink, mode 160000), and a
    .gitmodules naming any other submodule or URL;
  - files over 5 MB (ALLOW_LARGE lists the exceptions; none today);
  - PE and ELF images, LLVM bitcode, and the suffixes .exe .dll .so .ll .o .a .bc .lib .pdb;
  - an .obj that is not text (Wavefront meshes, roomrun/examples, are);
  - images, audio and video outside IMAGE_DIRS (our own Room Run previews, no game content).
Prints ROOT_CHECK_OK with the counts, or one line per problem and exits 1.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

MAX_BYTES = 5 * 1024 * 1024
ALLOW_LARGE: dict[str, str] = {}
FORBIDDEN_SUFFIXES = {".exe", ".dll", ".so", ".ll", ".o", ".a", ".bc", ".lib", ".pdb", ".sys"}
MEDIA_SUFFIXES = set(".png .jpg .jpeg .gif .bmp .webp .tga .tif .tiff .dds .wav .ogg .mp3 .flac .adx .ahx "
                     ".mp4 .mkv .webm .avi .mov .y4m .prs .pak .gvr .gvm .pvr .pvm .afs .csb .sfd".split())
# Images that are ours: the Room Run generator's previews of its own courses (no game content).
IMAGE_DIRS = ("roomrun/examples/png/",)
SUBMODULE = "external/OpenSA2"
SUBMODULE_URL = "https://github.com/jonnyczi/OpenSA2.git"


def git(repo: Path, *args: str, data: bytes | None = None) -> bytes:
    return subprocess.run(["git", "-C", str(repo), *args], input=data, capture_output=True, check=True).stdout


def blobs(repo: Path, shas: list[str]):
    """Yield (sha, bytes) for each blob, through one cat-file --batch."""
    if not shas:
        return
    out = git(repo, "cat-file", "--batch", data=("\n".join(shas) + "\n").encode())
    i = 0
    for _ in shas:
        nl = out.index(b"\n", i)
        sha, kind, size = out[i:nl].decode().split()
        size = int(size)
        yield sha, out[nl + 1:nl + 1 + size]
        i = nl + 1 + size + 1


def is_pe(b: bytes) -> bool:
    if len(b) < 64 or b[:2] != b"MZ":
        return False
    off = int.from_bytes(b[60:64], "little")
    return b[off:off + 4] == b"PE\0\0"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    a = ap.parse_args(argv)
    problems: list[str] = []
    entries = []
    for rec in git(a.repo, "ls-files", "-s", "-z").split(b"\0"):
        if not rec:
            continue
        meta, path = rec.decode("utf-8", "surrogateescape").split("\t", 1)
        mode, sha, _stage = meta.split()
        entries.append((mode, sha, path))
    files = [(m, s, p) for m, s, p in entries if m != "160000"]
    gitlinks = [p for m, s, p in entries if m == "160000"]
    for p in gitlinks:
        if p != SUBMODULE:
            problems.append(f"{p}: a submodule other than {SUBMODULE}")
    if SUBMODULE not in gitlinks:
        problems.append(f"{SUBMODULE}: not a submodule (gitlink) in the index")
    gm = a.repo / ".gitmodules"
    if gm.is_file():
        text = gm.read_text()
        paths = re.findall(r"^\s*path\s*=\s*(\S+)", text, re.M)
        urls = re.findall(r"^\s*url\s*=\s*(\S+)", text, re.M)
        if paths != [SUBMODULE] or urls != [SUBMODULE_URL]:
            problems.append(f".gitmodules: expected only {SUBMODULE} = {SUBMODULE_URL}, has {paths} {urls}")
    sizes = {}
    for line in git(a.repo, "cat-file", "--batch-check", data=("\n".join(s for _, s, _ in files) + "\n").encode()).decode().splitlines():
        sha, _kind, size = line.split()
        sizes[sha] = int(size)
    want_content = []
    for mode, sha, path in files:
        pp = PurePosixPath(path)
        low = path.lower()
        suffix = pp.suffix.lower()
        if path.startswith("media/"):
            problems.append(f"{path}: under media/ (game screenshots stay local)")
        if "private" in pp.parts[:-1]:
            problems.append(f"{path}: under a private/ directory")
        if path.startswith("external/"):
            problems.append(f"{path}: a file under external/ (only the {SUBMODULE} submodule may be there)")
        if mode == "120000":
            continue                                   # a symlink: its blob is the target path
        if sizes.get(sha, 0) > MAX_BYTES and path not in ALLOW_LARGE:
            problems.append(f"{path}: {sizes[sha]} bytes, over 5 MB (tools/release/root_check.py ALLOW_LARGE)")
        if suffix in FORBIDDEN_SUFFIXES or re.search(r"\.so(\.\d+)+$", low):
            problems.append(f"{path}: a forbidden suffix ({suffix})")
        if suffix in MEDIA_SUFFIXES and not path.startswith(IMAGE_DIRS):
            problems.append(f"{path}: an image/audio/video/game-data file outside {', '.join(IMAGE_DIRS)}")
        want_content.append((sha, path, suffix))
    by_sha = {}
    for sha, path, suffix in want_content:
        by_sha.setdefault(sha, []).append((path, suffix))
    for sha, data in blobs(a.repo, list(by_sha)):
        for path, suffix in by_sha[sha]:
            if is_pe(data):
                problems.append(f"{path}: a PE image")
            elif data[:4] == b"\x7fELF":
                problems.append(f"{path}: an ELF image")
            elif data[:4] in (b"BC\xc0\xde", b"\xde\xc0\x17\x0b"):
                problems.append(f"{path}: LLVM bitcode")
            elif suffix == ".obj" and b"\0" in data[:65536]:
                problems.append(f"{path}: a binary .obj (only Wavefront text meshes are tracked)")
    for line in problems:
        print(f"REFUSED {line}")
    if problems:
        print(f"ROOT_CHECK_FAILED {len(problems)} problem(s)")
        return 1
    print(f"ROOT_CHECK_OK {len(files)} files, submodule {SUBMODULE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
