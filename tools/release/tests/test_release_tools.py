#!/usr/bin/env python3
"""Tests of tools/release (sa2mr_scan.py, root_check.py) with planted fixtures; no game files.

    python3 -m unittest discover -s tools/release/tests -v
"""
from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parents[1]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


scan = load("sa2mr_scan")
root_check = load("root_check")


def elf(machine: int = 183, symbols: list[str] = (), raw: bytes = b"") -> bytes:
    """A minimal ELF64 little-endian file: a null section, .strtab, .symtab (the given names)."""
    strtab = b"\0" + b"".join(s.encode() + b"\0" for s in symbols)
    syms = b"\0" * 24
    off = 1
    for s in symbols:
        syms += struct.pack("<IBBHQQ", off, 0x12, 0, 1, 0x1000, 4)
        off += len(s) + 1
    body = raw + strtab + syms
    hdr_size = 64
    str_off = hdr_size + len(raw)
    sym_off = str_off + len(strtab)
    shoff = hdr_size + len(body)
    ident = b"\x7fELF" + bytes([2, 1, 1]) + b"\0" * 9
    hdr = ident + struct.pack("<HHIQQQIHHHHHH", 2, machine, 1, 0, 0, shoff, 0, 64, 0, 0, 64, 3, 1)
    sh = b"\0" * 64
    sh += struct.pack("<IIQQQQIIQQ", 0, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0)
    sh += struct.pack("<IIQQQQIIQQ", 0, 2, 0, 0, sym_off, len(syms), 1, 1, 8, 24)
    return hdr + body + sh


def pe() -> bytes:
    b = bytearray(b"MZ" + b"\0" * 200)
    b[60:64] = (128).to_bytes(4, "little")
    b[128:132] = b"PE\0\0"
    return bytes(b)


class ScanFixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.game_file = b"pretend this is a SEGA game file\n" * 10
        hashes = {"files": [{"path": "resource/gd_PC/fake.prs", "sha256": hashlib.sha256(self.game_file).hexdigest()}]}
        self.hashes = self.d / "hashes.json"
        self.hashes.write_text(json.dumps(hashes))
        self.bc_good = b"BC\xc0\xde" + b"\x01" * 100
        self.allow = self.d / "bc.txt"
        self.allow.write_text(f"{hashlib.sha256(self.bc_good).hexdigest()} x86.bc\n")
        self.tree = self.d / "tree"
        (self.tree / "bin").mkdir(parents=True)
        (self.tree / "share").mkdir()
        (self.tree / "bin" / "sa2recomp").write_bytes(elf(symbols=["main", "sub_helper"]))
        (self.tree / "share" / "README.txt").write_text("hello\n")
        (self.tree / "share" / "x86.bc").write_bytes(self.bc_good)

    def tearDown(self):
        self.tmp.cleanup()

    def run_scan(self, *paths, profile="app") -> tuple[int, str]:
        args = ["--profile", profile, "--opensa2", str(self.d / "no-opensa2"), "--forbidden-hashes",
                str(self.hashes), "--bc-allowlist", str(self.allow), *map(str, paths)]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = scan.main(args)
        return rc, out.getvalue()

    def plant(self, rel: str, data: bytes) -> None:
        p = self.tree / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def assert_refused(self, rel: str, data: bytes, why: str) -> None:
        self.plant(rel, data)
        rc, out = self.run_scan(self.tree)
        self.assertEqual(rc, 1, out)
        self.assertIn(why, out)
        self.assertIn("SCAN_FAILED", out)
        (self.tree / rel).unlink()

    def test_clean_tree_passes(self):
        rc, out = self.run_scan(self.tree)
        self.assertEqual(rc, 0, out)
        self.assertIn("SCAN_OK 3 files", out)

    def test_pe_refused_under_any_name(self):
        self.assert_refused("share/readme.dat", pe(), "a PE image")
        self.assert_refused("share/x.exe", b"text\n", "a Windows executable suffix")

    def test_build_outputs_refused(self):
        self.assert_refused("bin/sa2-game", elf(), "named sa2-game")
        for name in ("a.ll", "b.o", "c.a"):
            self.assert_refused(f"share/{name}", b"text\n", "build output")

    def test_bitcode_needs_the_allowlist(self):
        self.assert_refused("share/lifted.bc", b"BC\xc0\xde" + b"\x02" * 100, "not on the allowlist")
        # bitcode magic under another name
        self.assert_refused("share/lifted.dat", b"BC\xc0\xde" + b"\x03" * 100, "not on the allowlist")

    def test_lifted_symbols_refused(self):
        subs = [f"sub_{0x401000 + 16 * i:08x}" for i in range(60)]
        self.assert_refused("bin/innocent", elf(symbols=subs), "60 lifted-function symbols")
        self.assert_refused("lib/libx.so", elf(raw=b"".join(s.encode() + b"\0" for s in subs)),
                            "60 distinct sub_<hex> names")
        few = [f"sub_{0x401000 + 16 * i:08x}" for i in range(50)]
        self.plant("bin/ok", elf(symbols=few))
        self.assertEqual(self.run_scan(self.tree)[0], 0)

    def test_elf_rules(self):
        self.assert_refused("bin/x86", elf(machine=62), "not AArch64")
        self.assert_refused("share/tool", elf(), "an ELF outside")

    def test_game_file_hash_refused(self):
        self.assert_refused("share/renamed.txt", self.game_file, "is the game file resource/gd_PC/fake.prs")

    def test_media_refused(self):
        self.assert_refused("share/shot.png", b"text\n", "game-data suffix")
        self.assert_refused("share/shot.dat", b"\x89PNG\r\n\x1a\n" + b"\0" * 50, "a PNG image")
        self.assert_refused("share/bgm.adx", b"\x80\0", "game-data suffix")

    def test_private_and_size(self):
        self.assert_refused("kit/private/exe/notes.txt", b"x\n", "under a private/")
        self.assert_refused("share/big.txt", b"a" * ((120 << 20) + 1), "over the 120 MB cap")

    def test_unknown_binary_refused(self):
        self.assert_refused("share/blob", bytes(range(256)) * 4, "a binary file of no allowed type")

    def test_archives_are_read(self):
        tgz = self.d / "app.tar.gz"
        with tarfile.open(tgz, "w:gz") as t:
            for name, data in (("sa2mr/bin/sa2recomp", elf()), ("sa2mr/share/x", pe())):
                ti = tarfile.TarInfo(name)
                ti.size = len(data)
                t.addfile(ti, io.BytesIO(data))
            ti = tarfile.TarInfo("sa2mr/share/escape")
            ti.type, ti.linkname = tarfile.SYMTYPE, "../../../etc/passwd"
            t.addfile(ti)
        rc, out = self.run_scan(tgz)
        self.assertEqual(rc, 1)
        self.assertIn("app.tar.gz!sa2mr/share/x: a PE image", out)
        self.assertIn("symlink out of the tree", out)
        # a wheel (zip) holding a lifted .so, in the toolchain profile
        whl = self.d / "x-1.0-py3-none-any.whl"
        with zipfile.ZipFile(whl, "w") as z:
            z.writestr("x/_x.so", elf(symbols=[f"sub_{0x500000 + i:08x}" for i in range(51)]))
            z.writestr("x/__init__.py", "")
        rc, out = self.run_scan(whl, profile="toolchain")
        self.assertEqual(rc, 1)
        self.assertIn("51 lifted-function symbols", out)
        rc, out = self.run_scan(whl)
        self.assertIn("only the toolchain ships wheels", out)

    def test_real_receipts_load(self):
        opensa2 = ROOT / "external" / "OpenSA2"
        if not (opensa2 / "Documentation" / "receipts" / "install-hashes-v1.json").is_file():
            self.skipTest("no OpenSA2 checkout")
        forbidden = scan.load_forbidden(opensa2, [])
        self.assertGreater(len(forbidden), 3000)
        self.assertIn("4b1e09d782a2d919cbed2324ba2c0c78ef5fbe1677d400edab39608191f12c16", forbidden)
        bc = scan.load_bc_allowlist(ROOT / "build-env" / "remill-arm64" / "sources.lock", [])
        self.assertEqual(len(bc), 10)        # 13 files, 3 duplicates


class RootCheckFixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.r = Path(self.tmp.name)
        self.git("init", "-q")
        (self.r / ".gitmodules").write_text(
            '[submodule "external/OpenSA2"]\n\tpath = external/OpenSA2\n\turl = https://github.com/jonnyczi/OpenSA2.git\n')
        (self.r / "README.md").write_text("hi\n")
        self.git("add", ".gitmodules", "README.md")
        self.git("update-index", "--add", "--cacheinfo", "160000," + "1" * 40 + ",external/OpenSA2")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *a):
        subprocess.run(["git", "-C", str(self.r), *a], check=True, capture_output=True)

    def check(self) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = root_check.main(["--repo", str(self.r)])
        return rc, out.getvalue()

    def add(self, rel: str, data: bytes) -> None:
        p = self.r / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        self.git("add", "-f", rel)

    def test_clean(self):
        rc, out = self.check()
        self.assertEqual(rc, 0, out)
        self.assertIn("ROOT_CHECK_OK", out)

    def test_refusals(self):
        cases = [("media/shot.txt", b"x", "under media/"),
                 ("external/other/a.txt", b"x", "a file under external/"),
                 ("tools/game.dat", pe(), "a PE image"),
                 ("tools/tool", elf(), "an ELF image"),
                 ("tools/x.ll", b"x", "forbidden suffix"),
                 ("docs/shot.png", b"x", "outside roomrun/examples/png/"),
                 ("roomrun/examples/m.obj", b"\0\1\2", "a binary .obj"),
                 ("big.txt", b"a" * (5 * 1024 * 1024 + 1), "over 5 MB")]
        for rel, data, why in cases:
            with self.subTest(rel=rel):
                self.add(rel, data)
                rc, out = self.check()
                self.assertEqual(rc, 1, out)
                self.assertIn(why, out)
                self.git("rm", "-q", "--cached", rel)

    def test_allowed(self):
        self.add("roomrun/examples/png/course.png", b"\x89PNG\r\n\x1a\n")
        self.add("roomrun/examples/course.obj", b"o track\nv 0 0 0\n")
        rc, out = self.check()
        self.assertEqual(rc, 0, out)

    def test_missing_submodule(self):
        self.git("rm", "-q", "--cached", "external/OpenSA2")
        rc, out = self.check()
        self.assertIn("not a submodule", out)


if __name__ == "__main__":
    unittest.main()
