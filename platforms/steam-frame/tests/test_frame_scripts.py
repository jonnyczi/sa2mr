#!/usr/bin/env python3
"""Unit tests of the Steam Frame scripts (stdlib unittest; no Steam, no game files).

    python3 -m unittest discover -s platforms/steam-frame/tests -v
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
FRAME = HERE.parent


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, FRAME / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


A = load("add_to_steam")
B = load("sa2mr_build")


def raw_entry(i: int, name: str, exe: str) -> bytes:
    """One shortcut written by hand (not by add_to_steam's writer), with a uint64 and a float
    field, the kinds a Steam-written file may hold."""
    s = lambda k, v: b"\x01" + k.encode() + b"\0" + v.encode() + b"\0"   # noqa: E731
    n = lambda k, v: b"\x02" + k.encode() + b"\0" + struct.pack("<i", v)  # noqa: E731
    return (b"\x00" + str(i).encode() + b"\0" + n("appid", -12345 - i) + s("AppName", name) + s("Exe", exe)
            + s("StartDir", '"/opt/x/"') + n("OpenVR", 0) + b"\x07LastPlayTimeU64\0" + struct.pack("<Q", 2**40 + i)
            + b"\x03Scale\0" + struct.pack("<f", 1.5) + b"\x00tags\0" + s("0", "favorite") + b"\x08" + b"\x08")


def synthetic(entries: list[tuple[str, str]]) -> bytes:
    return b"\x00shortcuts\0" + b"".join(raw_entry(i, n, e) for i, (n, e) in enumerate(entries)) + b"\x08\x08"


class AddToSteam(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.app = t / "Applications" / "sa2mr"
        (self.app / "share").mkdir(parents=True)
        (self.app / "sa2mr-vr.sh").write_text("#!/bin/sh\n")
        self.steam = t / "Steam"
        self.cfg = self.steam / "userdata" / "1234" / "config"
        self.cfg.mkdir(parents=True)
        self.orig = synthetic([("Other Game", '"/opt/other/run.sh"'), ("Emulator", '"/usr/bin/emu"')])
        (self.cfg / "shortcuts.vdf").write_bytes(self.orig)

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, *extra) -> str:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            rc = A.main(["--app", str(self.app), "--steam-root", str(self.steam), *extra])
        self.assertEqual(rc, 0)
        self.assertIn("SA2MR_STEAM_ENTRY_OK", out.getvalue())
        return out.getvalue()

    def test_round_trip_is_byte_identical(self):
        self.assertEqual(A.dump_shortcuts(A.parse_shortcuts(self.orig)), self.orig)

    def test_add_keeps_others_and_sets_openvr(self):
        out = self.run_main()
        data = (self.cfg / "shortcuts.vdf").read_bytes()
        root = A.parse_shortcuts(data)
        self.assertEqual(list(root), ["0", "1", "2"])
        before = A.parse_shortcuts(self.orig)
        # the two other entries kept, value for value and type for type, in their order
        self.assertEqual(A.write_map(root["0"]), A.write_map(before["0"]))
        self.assertEqual(A.write_map(root["1"]), A.write_map(before["1"]))
        self.assertIsInstance(root["0"]["LastPlayTimeU64"], A.U64)
        self.assertIsInstance(root["1"]["Scale"], A.F32)
        mine = root["2"]
        exe = f'"{self.app.resolve() / "sa2mr-vr.sh"}"'
        self.assertEqual(mine["AppName"], "Sonic Adventure 2 VR")
        self.assertEqual(mine["Exe"], exe)
        self.assertEqual(mine["OpenVR"], 1)
        self.assertEqual(mine["StartDir"], f'"{self.app.resolve()}/"')
        appid, game_id = A.shortcut_ids(exe, "Sonic Adventure 2 VR")
        self.assertEqual(mine["appid"], appid)
        self.assertTrue(appid & 0x80000000 or appid < 0)
        self.assertIn(f"steam://rungameid/{game_id}", out)
        self.assertEqual((self.cfg / "shortcuts.vdf.bak").read_bytes(), self.orig)
        self.assertFalse(list(self.cfg.glob("*.tmp")))

    def test_again_replaces_by_exe(self):
        self.run_main()
        first = (self.cfg / "shortcuts.vdf").read_bytes()
        self.run_main()
        root = A.parse_shortcuts((self.cfg / "shortcuts.vdf").read_bytes())
        self.assertEqual(len(root), 3)
        self.assertEqual((self.cfg / "shortcuts.vdf").read_bytes(), first)

    def test_remove(self):
        self.run_main()
        self.run_main("--remove")
        root = A.parse_shortcuts((self.cfg / "shortcuts.vdf").read_bytes())
        self.assertEqual([e["AppName"] for e in root.values()], ["Other Game", "Emulator"])
        self.assertEqual((self.cfg / "shortcuts.vdf").read_bytes(), self.orig)

    def test_new_file(self):
        (self.cfg / "shortcuts.vdf").unlink()
        self.run_main()
        root = A.parse_shortcuts((self.cfg / "shortcuts.vdf").read_bytes())
        self.assertEqual(len(root), 1)
        self.assertFalse((self.cfg / "shortcuts.vdf.bak").exists())

    def test_refuses_a_bad_file(self):
        (self.cfg / "shortcuts.vdf").write_bytes(b"\x00shortcuts\0\x09junk")
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()), \
                contextlib.redirect_stdout(io.StringIO()):
            A.main(["--app", str(self.app), "--steam-root", str(self.steam)])
        self.assertEqual((self.cfg / "shortcuts.vdf").read_bytes(), b"\x00shortcuts\0\x09junk")


def fake_kit(root: Path) -> Path:
    kit = root / "kit"
    for rel, text in {
        "tools/s4/rebuild_image.py": "# rebuild\n",
        "tools/s4/lift_ir_receipt.py": "# receipt\n",
        "tools/pins.json": "{}\n",
        "requirements.txt": "pefile\n",
        "sa2mr/map/restore_map.py": "# restore\n",
        "sa2mr/finalize_extras.py": "# finalize\n",
        "sa2mr/arm64/lift_arm64.py": "# lift\n",
        "runtime/sa2_main.c": "int main(void) { return 0; }\n",
        B.TRIMMED_MAP: "{}\n",
        B.TRANSLATION_INPUTS: "# test\nDocumentation/sa2mr/function-map-trimmed.json\nsa2mr/map/*.py\n"
                              "sa2mr/finalize_extras.py\nsa2mr/arm64/lift_arm64.py\ntools/s4/**/*\n"
                              "tools/pins.json\nrequirements.txt\n",
    }.items():
        (kit / rel).parent.mkdir(parents=True, exist_ok=True)
        (kit / rel).write_text(text)
    return kit


class Builder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = Path(self.tmp.name)
        self.kit = fake_kit(self.t)
        self.tc = self.t / "toolchain"
        for p in ("remill/bin/remill-lift-14", "remill/share/remill/14/semantics/x86.bc", "zig/zig",
                  "wheels/pefile-2024.8.26-py3-none-any.whl"):
            (self.tc / p).parent.mkdir(parents=True, exist_ok=True)
            (self.tc / p).write_text(p)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, **kv) -> "B.Build":
        ns = B.argparse.Namespace(kit=str(self.kit), toolchain=str(self.tc), app=str(self.t / "app"),
                                  data=str(self.t / "data"), steam_root=str(self.t / "Steam"), jobs=2,
                                  from_step=None, only=None, dry_run=True, proton=None, slr=None,
                                  no_steam=False, no_stop_steam=False, no_notify=True, no_wait_install=True,
                                  allow_any_arch=True)
        for k, v in kv.items():
            setattr(ns, k, v)
        return B.Build(ns)

    def test_translation_key(self):
        k1 = self.build().tkey()
        self.assertEqual(k1, self.build().tkey())
        (self.kit / "runtime/sa2_main.c").write_text("int main(void) { return 1; }\n")
        self.assertEqual(k1, self.build().tkey(), "a runtime change relinks; it is not a translation input")
        (self.kit / "tools/s4/rebuild_image.py").write_text("# rebuild 2\n")
        k2 = self.build().tkey()
        self.assertNotEqual(k1, k2)
        (self.tc / "zig/zig").write_text("another zig")
        self.assertNotEqual(k2, self.build().tkey(), "the toolchain is in the key")
        (self.tc / "MANIFEST.sha256").write_text("x  zig/zig\n")
        k3 = self.build().tkey()
        (self.tc / "MANIFEST.sha256").write_text("y  zig/zig\n")
        self.assertNotEqual(k3, self.build().tkey(), "with a manifest, the manifest is the toolchain's identity")

    def test_missing_named_input_refused(self):
        (self.kit / B.TRANSLATION_INPUTS).write_text("tools/nothing_here.py\n")
        with self.assertRaises(B.StepError):
            self.build().tkey()

    def test_jobs(self):
        j = B.default_jobs()
        self.assertTrue(1 <= j <= 7)
        os.environ["SA2MR_JOBS"] = "3"
        try:
            self.assertEqual(B.default_jobs(), 3)
        finally:
            del os.environ["SA2MR_JOBS"]

    def test_step_names(self):
        self.assertEqual([s[0] for s in B.STEPS], list(range(15)))
        self.assertEqual(B.step_number("link"), 13)
        self.assertEqual(B.step_number("6"), 6)

    def test_library_folders(self):
        vdf = '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"/home/u/.local/share/Steam"\n\t}\n' \
              '\t"1"\n\t{\n\t\t"path"\t\t"/run/media/sd/SteamLibrary"\n\t}\n}\n'
        self.assertEqual(B.vdf_text_paths(vdf), ["/home/u/.local/share/Steam", "/run/media/sd/SteamLibrary"])

    def test_install_manifest(self):
        d = self.t / "xdg" / "sa2recomp"
        d.mkdir(parents=True)
        (d / "manifest.tsv").write_text("sa2recomp-manifest 1\ntable\tinstall-hashes-v1\tabc\n"
                                        "root\t/games/Sonic Adventure 2\nverified\tnow\n")
        old = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = str(self.t / "xdg")
        try:
            m, root = B.install_manifest_root()
        finally:
            if old is None:
                del os.environ["XDG_DATA_HOME"]
            else:
                os.environ["XDG_DATA_HOME"] = old
        self.assertEqual(root, Path("/games/Sonic Adventure 2"))

    def test_cache_and_state(self):
        b = self.build(dry_run=False)
        self.assertFalse(b.cache_ok())
        files = b.cache_dir() / "files"
        for rel in B.CACHE_KEEP:
            (files / rel).mkdir(parents=True)
        (b.cache_dir() / "cache.json").write_text(json.dumps({"key": b.tkey()}))
        self.assertTrue(b.cache_ok())
        b.write_state(6, "translate", {"ok": True, "digest": b.tkey()})
        self.assertEqual(b.read_state(6, "translate")["digest"], b.tkey())
        b.clear_states(range(5, 13))
        self.assertEqual(b.read_state(6, "translate"), {})

    def test_place_tree_links(self):
        src = self.t / "src"
        (src / "a/b").mkdir(parents=True)
        (src / "a/b/f.o").write_bytes(b"obj")
        dst = self.t / "dst"
        self.assertEqual(B.place_tree(src, dst), 3)
        self.assertTrue((dst / "a/b/f.o").samefile(src / "a/b/f.o"))

    def test_dry_run_prints_the_plan(self):
        r = subprocess.run([sys.executable, str(FRAME / "sa2mr_build.py"), "--dry-run", "--allow-any-arch",
                            "--no-notify", "--kit", str(self.kit), "--toolchain", str(self.tc),
                            "--app", str(self.t / "app"), "--data", str(self.t / "data"),
                            "--steam-root", str(self.t / "Steam"), "--jobs", "3"],
                           capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        for n, name, title, *_ in B.STEPS:
            self.assertIn(f"[{n:2d}] {title}", r.stdout)
        for cmd in ("tools/s4/rebuild_image.py --jobs 3", "tools/s4/lift_ir_receipt.py",
                    "sa2mr/arm64/lift_arm64.py --target aarch64-linux-musl --mode double",
                    "release_build_arm64.py --only tables", "release_build_arm64.py --only game --f80-cached",
                    "sa2-install --source steam --yes", "add_to_steam.py"):
            self.assertIn(cmd, r.stdout)
        self.assertFalse((self.t / "data").exists(), "a dry run writes nothing")


if __name__ == "__main__":
    unittest.main()
