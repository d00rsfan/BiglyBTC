#!/usr/bin/env python3
"""Deploy and archive the Windows desktop and its architecture-specific runtime."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile


MACHINE = {"x64": 0x8664, "arm64": 0xAA64}
SDK = "bin/torrent-rasterbar.dll"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pe_machine(path):
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            raise ValueError(f"Invalid PE DOS header in {path}")
        stream.seek(struct.unpack_from("<I", dos, 60)[0])
        pe = stream.read(26)
        if len(pe) != 26 or pe[:4] != b"PE\0\0" or pe[24:26] != b"\x0b\x02":
            raise ValueError(f"Expected a 64-bit Windows image in {path}")
        return struct.unpack_from("<H", pe, 4)[0]


def inventory(root, arch):
    required = {
        "bin/BiglyBTC.exe", "bin/BiglyBTC-headless.exe", SDK,
        "bin/Qt6Core.dll", "bin/Qt6Gui.dll", "bin/Qt6Network.dll", "bin/Qt6Widgets.dll",
        "bin/platforms/qwindows.dll", "bin/qt.conf",
        "lib/biglybtc/lib/BiglyBTC-plugin-SpeedScheduler.dll",
        "lib/biglybtc/lib/BiglyBTC-plugin-azlocprov.dll",
        "share/BiglyBTC/geoip/DBIP-Country.mmdb", "share/BiglyBTC/geoip/DBIP-ASN.mmdb",
        "share/doc/biglybtc/release-sdk.json", "share/doc/biglybtc/LICENSE",
    }
    files = {}
    labels = set()
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.is_symlink() or path.is_junction():
            raise ValueError(f"Unexpected link in Windows package at {name}")
        if not path.is_file():
            continue
        if name.casefold() in labels:
            raise ValueError(f"Case-insensitive package path collision at {name}")
        labels.add(name.casefold())
        if path.suffix.lower() in {".exe", ".dll"} and pe_machine(path) != MACHINE[arch]:
            raise ValueError(f"Wrong Windows architecture in {name}")
        files[name] = sha256(path)
    if extra := required - files.keys():
        raise ValueError(f"Missing Windows package files: {sorted(extra)}")
    receipt = json.loads((root / "share/doc/biglybtc/release-sdk.json").read_text(encoding="utf-8"))
    if (receipt["soname"].lower() != Path(SDK).name or receipt["sha256"] != files[SDK]
            or receipt["bytes"] != (root / SDK).stat().st_size):
        raise ValueError("Packaged libtorrent does not match the approved release image")
    return files


def bundle(args):
    qt_bin = args.windeployqt.resolve(strict=True).parent
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="biglybtc-win-", dir=args.build) as temporary:
        root = Path(temporary)
        subprocess.run([args.cmake, "--install", args.build, "--config", args.config, "--prefix", root], check=True)
        bin_dir = root / "bin"
        environment = dict(os.environ)
        environment["PATH"] = str(qt_bin) + os.pathsep + environment["PATH"]
        subprocess.run([args.windeployqt, "--release", "--no-compiler-runtime", "--dir", bin_dir,
                        "--plugindir", bin_dir, bin_dir / "BiglyBTC.exe", bin_dir / "BiglyBTC-headless.exe"],
                       env=environment, check=True)
        (bin_dir / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="ascii")
        for path in sorted(args.vendor_bin.glob("*.dll")):
            if (bin_dir / path.name).exists():
                raise ValueError(f"Duplicate Windows dependency {path.name}")
            shutil.copyfile(path, bin_dir / path.name)
        docs = root / "share/doc/biglybtc"
        for source in sorted(args.vendor_share.glob("*/copyright")):
            shutil.copyfile(source, docs / (source.parent.name + "-copyright"))
        (root / "README.txt").write_text(
            "Extract the entire archive, then open bin\\BiglyBTC.exe.\n"
            "Install the Microsoft Visual C++ Redistributable for this architecture if Windows asks for it.\n"
            "https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist\n",
            encoding="ascii")
        files = inventory(root, args.arch)
        manifest = root / "SHA256SUMS"
        manifest.write_bytes("".join(f"{value}  {name}\n" for name, value in files.items()).encode("utf-8"))
        candidate = args.output.with_suffix(".zip.tmp")
        try:
            with zipfile.ZipFile(candidate, "w", zipfile.ZIP_DEFLATED) as archive:
                for name in (*files, manifest.name):
                    archive.write(root / name, name)
            os.replace(candidate, args.output)
        finally:
            candidate.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "cmake", "windeployqt", "vendor-bin", "vendor-share", "output"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--arch", choices=MACHINE, required=True)
    parser.add_argument("--config", choices=("Release", "RelWithDebInfo"), required=True)
    bundle(parser.parse_args())
