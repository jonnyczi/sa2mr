#!/usr/bin/env python3
"""Assemble the iced-x86 aarch64 wheel from its sdist tree and the cross-built extension, the way
setuptools-rust lays it out (iced_x86/ package + iced_x86/_iced_x86_py.abi3.so), deterministically:
sorted entries, fixed timestamps and modes.
    mkwheel.py SDIST_DIR EXTENSION_SO MANYLINUX(e.g. 2_17) OUTDIR
"""
import base64
import hashlib
import pathlib
import sys
import zipfile

src, so, ml, out = sys.argv[1:]
src, so, out = pathlib.Path(src), pathlib.Path(so), pathlib.Path(out)
pkg_info = (src / "PKG-INFO").read_text()
version = next(l.split(":", 1)[1].strip() for l in pkg_info.splitlines() if l.startswith("Version:"))
tags = [f"cp38-abi3-manylinux_{ml}_aarch64"] + (["cp38-abi3-manylinux2014_aarch64"] if ml == "2_17" else [])
plat = ".".join(t.split("-", 2)[2] for t in tags)
name = f"iced_x86-{version}-cp38-abi3-{plat}.whl"
di = f"iced_x86-{version}.dist-info"

files: dict[str, bytes] = {}
for f in sorted((src / "src" / "iced_x86").iterdir()):
    if f.is_file() and not f.name.endswith(".pyc"):
        files[f"iced_x86/{f.name}"] = f.read_bytes()
files["iced_x86/_iced_x86_py.abi3.so"] = so.read_bytes()
files[f"{di}/METADATA"] = pkg_info.encode()
files[f"{di}/WHEEL"] = ("Wheel-Version: 1.0\nGenerator: sa2mr mkwheel.py\nRoot-Is-Purelib: false\n"
                        + "".join(f"Tag: {t}\n" for t in tags)).encode()
files[f"{di}/top_level.txt"] = b"iced_x86\n"
files[f"{di}/LICENSE.txt"] = (src / "LICENSE.txt").read_bytes()


def h(b: bytes) -> str:
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(b).digest()).rstrip(b"=").decode()


record = "".join(f"{p},{h(b)},{len(b)}\n" for p, b in files.items()) + f"{di}/RECORD,,\n"
files[f"{di}/RECORD"] = record.encode()
with zipfile.ZipFile(out / name, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p, b in files.items():
        zi = zipfile.ZipInfo(p, (1980, 1, 1, 0, 0, 0))
        zi.external_attr = (0o100755 if p.endswith(".so") else 0o100644) << 16
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, b, compresslevel=9)
print(out / name)
