#!/usr/bin/env python3
"""The release driver has hard references to the SDK's peer-class API."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    driver, distro = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="biglybtc-distro-sdk-") as directory:
        override = Path(directory) / "libtorrent-rasterbar.so.2.0"
        override.symlink_to(Path(distro).resolve(strict=True))
        environment = dict(os.environ, LC_ALL="C", LD_LIBRARY_PATH=directory)
        result = subprocess.run([driver, "unbound"], env=environment, capture_output=True, text=True, timeout=10)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode == 0:
        return
    expected = "undefined symbol: _ZN10libtorrent15peer_class_pool2atENS_3aux14strong_typedefIjNS_14peer_class_tagEvEE"
    if result.returncode != 127 or expected not in result.stderr:
        raise SystemExit(result.returncode if result.returncode > 0 else 1)
    print("The dynamic loader rejected the distro SDK: the release peer-class API is missing")


if __name__ == "__main__":
    main()
