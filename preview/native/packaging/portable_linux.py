#!/usr/bin/env python3
"""Build and smoke-test a relocatable archive from the native bundled Qt DEB."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

RUNTIME = {
    "libtorrent-rasterbar.so.2.0": "libtorrent-rasterbar2.0t64",
    "libssl.so.3": "libssl3t64",
    "libcrypto.so.3": "libssl3t64",
    "libstdc++.so.6": "libstdc++6",
    "libgcc_s.so.1": "libgcc-s1",
    "libxml2.so.16": "libxml2-16",
    "libnettle.so.8": "libnettle8t64",
    "libbz2.so.1.0": "libbz2-1.0",
}


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, timeout=90, **kwargs)


def smoke_desktop(launcher, environment, release_sdk):
    # The offline desktop accepts generated profiles under /tmp only.
    with tempfile.TemporaryDirectory(prefix="biglybtc-portable-smoke-", dir="/tmp") as temporary:
        profile = Path(temporary) / ".biglybt" if release_sdk else Path(temporary)
        (profile / "active").mkdir(parents=True)
        (profile / "biglybt.config").write_bytes(b"de")
        (profile / "downloads.config").write_bytes(b"d9:downloadslee")
        env = dict(environment, QT_QPA_PLATFORM="offscreen", HOME=temporary)
        command = [launcher] if release_sdk else [launcher, "--experimental-local", profile, "0"]
        with subprocess.Popen(command, cwd=profile,
                              env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as app:
            try:
                _, error = app.communicate(timeout=10 if release_sdk else 3)
            except subprocess.TimeoutExpired:
                app.terminate()
                try:
                    _, error = app.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.communicate()
                    raise RuntimeError("Relocated desktop failed to stop")
            else:
                raise RuntimeError("Relocated desktop exited during startup: " + error.decode(errors="replace"))
        if release_sdk and (not (profile / ".azlock").is_file()
                or b"Library opening: ready" not in error or b"Public chat transport: ready" not in error):
            raise RuntimeError("Relocated desktop could not start the engine and chat: " + error.decode(errors="replace"))


def make_archive(deb, destination, notices_root):
    version = run("dpkg-deb", "-f", deb, "Version").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+~dev(?:\.[1-9][0-9]*)?", version):
        raise ValueError("Unexpected DEB version")
    architecture = run("dpkg-deb", "-f", deb, "Architecture").strip()
    if architecture not in {"amd64", "arm64"} or architecture != run("dpkg", "--print-architecture").strip():
        raise ValueError("Portable export requires a package for the native build architecture")
    name = f"BiglyBTC-{version}-linux-{architecture}"
    output = destination / f"{name}.tar.gz"
    with tempfile.TemporaryDirectory(prefix="biglybtc-portable-", dir=destination) as temporary:
        work = Path(temporary)
        run("dpkg-deb", "--extract", deb, work / "deb")
        staged = work / "deb/usr/lib/biglybtc"
        qt = staged / "qt/lib"
        if not (qt / "libQt6Core.so.6").is_file():
            raise ValueError("Portable export requires the bundled Qt package")
        root = work / name
        staged.rename(root)
        docs = root / "licenses"
        shutil.move(work / "deb/usr/share/doc/biglybtc", docs)
        # The packaging receipt has build-host paths. Notices and copyright
        # files are in the portable tree and the receipt stays with CI outputs.
        (docs / "qt-runtime.json").unlink()
        release_sdk = (docs / "release-sdk.json").is_file()
        if release_sdk:
            (root / "share").mkdir()
            shutil.move(work / "deb/usr/share/BiglyBTC", root / "share/BiglyBTC")
            (root / "share/BiglyBTC/geoip").chmod(0o700)
        binary = root / "bin/BiglyBTC"
        loader = run("ldd", binary)
        libraries = dict(re.findall(r"^\s*(\S+)\s+=>\s+(/\S+)", loader, re.MULTILINE))
        for soname, package in RUNTIME.items():
            if soname not in libraries:
                raise ValueError(f"Could not resolve {soname} from the built desktop")
            if release_sdk and soname == "libtorrent-rasterbar.so.2.0":
                if Path(libraries[soname]).resolve() != (root / "lib" / soname).resolve():
                    raise ValueError("Portable desktop resolved the wrong release SDK")
                continue
            shutil.copyfile(libraries[soname], root / "lib" / soname)
            notice = Path("/usr/share/doc") / package / "copyright"
            if not notice.is_file():
                notice = notices_root / notice.relative_to("/")
            shutil.copyfile(notice, docs / f"{package}-copyright")
        launcher = root / "BiglyBTC"
        launcher.write_text(
            '#!/bin/sh\nset -eu\n'
            'root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\n'
            'export LD_LIBRARY_PATH="$root/lib:$root/qt/lib"\n'
            'exec "$root/bin/BiglyBTC" "$@"\n')
        launcher.chmod(0o755)
        (root / "README.txt").write_text(
            f"BiglyBTC Linux {architecture} portable archive\n\n"
            "This build targets Ubuntu 26.04 or a compatible Linux system. It includes Qt, ICU,\n"
            "the torrent engine and selected shared libraries. The host provides glibc,\n"
            "graphics and window-system libraries, CA certificates and standard shell tools.\n"
            "Extract the directory and launch BiglyBTC from its top level.\n"
            "Copying the complete directory preserves its relative runtime paths.\n")
        run("tar", "-czf", work / output.name, "-C", work, name)
        relocated = work / "unpacked elsewhere"
        relocated.mkdir()
        run("tar", "-xzf", work / output.name, "-C", relocated)
        unpacked = relocated / name
        profile = work / "profile"
        profile.mkdir(mode=0o700)
        (profile / "active").mkdir()
        (profile / "biglybt.config").write_bytes(b"de")
        (profile / "downloads.config").write_bytes(b"d9:downloadslee")
        env = {key: value for key, value in os.environ.items()
               if key not in {"LD_LIBRARY_PATH", "LD_PRELOAD", "LD_AUDIT", "QT_PLUGIN_PATH"}}
        report = json.loads(run(unpacked / "BiglyBTC", "--check-profile", profile, env=env))
        if report.get("status") != "METADATA_READABLE" or report.get("writes") != 0:
            raise ValueError("Relocated archive did not pass the profile preflight")
        smoke_desktop(unpacked / "BiglyBTC", env, release_sdk)
        (work / output.name).replace(output)
    print(f"Verified relocated {output.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("deb", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--notices-root", type=Path, default=Path("/"))
    args = parser.parse_args()
    make_archive(args.deb.resolve(strict=True), args.destination.resolve(strict=True), args.notices_root)
