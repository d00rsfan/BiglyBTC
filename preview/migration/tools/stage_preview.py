#!/usr/bin/env python3
"""Stage a directly runnable development folder, without creating or installing a DEB."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libtorrent-package", type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.absolute()
    package = args.libtorrent_package.resolve()
    if destination.exists() or destination.is_symlink():
        parser.error("Use a fresh distribution directory; existing builds are preserved")
    identity = subprocess.check_output(["dpkg-deb", "--field", str(package),
        "Package", "Version", "Architecture"], text=True)
    if identity != "Package: libtorrent-rasterbar2.0t64\nVersion: 2.0.12-1\nArchitecture: amd64\n":
        parser.error("Expected the Ubuntu amd64 libtorrent 2.0.12 runtime package")
    destination.mkdir(parents=True)
    environment = os.environ.copy()
    environment["DESTDIR"] = str(destination)
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", "/usr", "--strip"],
                   env=environment, check=True, timeout=180)
    dependency = destination / "dependencies/libtorrent"
    dependency.parent.mkdir()
    subprocess.run(["dpkg-deb", "--extract", str(package), str(dependency)], check=True, timeout=60)
    launcher = destination / "BiglyBTC-preview"
    launcher.write_text("#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\n"
        "root = Path(__file__).resolve().parent\n"
        "os.environ['LD_LIBRARY_PATH'] = str(root / 'dependencies/libtorrent/usr/lib/x86_64-linux-gnu')\n"
        "os.execv(str(root / 'usr/bin/BiglyBTC-preview'), ['BiglyBTC-preview', *sys.argv[1:]])\n")
    launcher.chmod(0o755)
    (destination / "README.txt").write_text(
        "BiglyBTC offline desktop preview\n\n"
        "Run ./BiglyBTC-preview from this folder. Qt and the libtorrent runtime\n"
        "are included alongside the application; no system installation is needed.\n"
        "The four torrents are generated examples. Transfers are disabled.\n"
        "Check My Torrents/Ctrl+1, My Tracker/Ctrl+2, My Shares/Ctrl+3, sorting and Files/Ctrl+I.\n"
        "Saved service views are read-only; no tracker or share service is started.\n"
        "Also check Refresh/F5, scaling, auxiliary tab closure and window close.\n"
        "Each launch uses a fresh /tmp/biglybtc-preview-* sample profile.\n"
        "The original ~/.biglybt profile is never opened by this launcher.\n"
        "Full client/plugin migration and original-profile switching remain unfinished.\n")
    environment["LD_LIBRARY_PATH"] = str(dependency / "usr/lib/x86_64-linux-gnu")
    linked = subprocess.check_output(["ldd", str(destination / "usr/lib/biglybtc/bin/BiglyBTC")],
                                     env=environment, text=True, timeout=30)
    if "not found" in linked or "/opt/Qt" in linked or "/tmp/" in linked:
        raise ValueError("Distribution retains a missing or build-only dependency: " + linked)
    files = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            files[path.relative_to(destination).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (destination / "BUILD_MANIFEST.json").write_text(json.dumps({
        "scope": "Offline generated-sample development preview; full migration remains unfinished",
        "build": str(args.build.resolve()), "launch": "./BiglyBTC-preview",
        "dependency_package_sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
        "files_sha256": files, "loader": linked, "system_installation": False,
    }, indent=2) + "\n")
    print("Runnable preview:", launcher)


if __name__ == "__main__":
    main()
