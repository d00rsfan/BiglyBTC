#!/usr/bin/env python3
"""Open the offline BiglyBTC preview using fresh generated data only."""

import argparse
import gzip
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


def encode(value):
    if isinstance(value, str):
        value = value.encode("utf-8")
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, list):
        return b"l" + b"".join(map(encode, value)) + b"e"
    if isinstance(value, dict):
        return b"d" + b"".join(encode(key) + encode(value[key]) for key in sorted(value)) + b"e"
    raise TypeError(type(value))


def validate_root(root):
    if root.parent != Path("/tmp") or not root.name.startswith("biglybtc-") or root.resolve() != root:
        raise ValueError("The preview requires a generated /tmp/biglybtc-* directory")


def prepare(root):
    """Populate a new, private sample profile; never import an existing profile."""
    validate_root(root)
    profile = root / "profile"
    active = profile / "active"
    active.mkdir(parents=True, mode=0o700)
    payload = root / "payload"
    payload.mkdir(mode=0o700)
    downloads, cache, paused = [], [], []
    samples = [
        ("Welcome to BiglyBTC.txt", 0, 1, 1),
        ("Sample music — ש العربية", 70, 0, 2),
        ("Example documents", 75, 0, 2),
        ("Paused sample.bin", 0, 0, 1),
    ]
    for index, (name, state, complete, count) in enumerate(samples):
        size = (index + 1) * 32768
        info = {"name": name, "piece length": 16384,
                "pieces": hashlib.sha1(b"synthetic preview piece").digest() * (size // 16384)}
        if count == 1:
            info["length"] = size
        else:
            info["files"] = [{"length": size // 2, "path": ["Read me.txt"]},
                             {"length": size // 2, "path": ["Examples", "Unicode — ש.txt"]}]
        identity = hashlib.sha1(encode(info)).digest()
        torrent = {"info": info, "attributes": {"displayname": name}}
        torrent_path = active / (identity.hex().upper() + ".dat")
        torrent_path.write_bytes(encode(torrent))
        downloads.append({"torrent": str(torrent_path), "torrent_hash": identity,
                          "save_dir": str(payload), "save_file": name,
                          "persistent": 1, "state": state, "position": index + 1,
                          "completedbytes": size if complete else size // 4,
                          "downloaded": size * 2, "uploaded": size // 2})
        cache.append({"hash": identity, "name": name, "size": size, "fc": count, "tt": 1})
        if index == 3:
            paused.append({"hash": identity, "force": 0, "owner": 1})
    (profile / "downloads.config").write_bytes(encode({"downloads": downloads, "pause_data": paused}))
    (active / "cache.dat").write_bytes(gzip.compress(encode({"state": cache}), mtime=0))
    # Saved service settings are synthetic display data. The preview never starts
    # a tracker, scans a share, resolves a cache path or creates payload files.
    (profile / "tracker.config").write_bytes(encode({"torrents": [
        {"hash": cache[0]["hash"], "status": 1, "persistent": 1, "passive": 0,
         "dateadded": 1700000000000, "torrent_file": str(active / "synthetic-host.torrent"),
         "stats": {"completed": 3, "announces": 12, "uploaded": 65536,
                   "downloaded": 32768, "scrapes": 4}},
        {"hash": hashlib.sha1(b"synthetic saved statistics").digest(),
         "persistent": 0, "passive": 0, "backup_time": 1700000000000,
         "stats": {"completed": 1, "announces": 5, "uploaded": 16384, "downloaded": 8192}},
    ]}))
    (profile / "sharing.config").write_bytes(encode({"resources": [
        {"type": 1, "file": str(payload / "Synthetic shared file.txt"),
         "ihash": hashlib.sha1(b"synthetic file fingerprint").digest(),
         "ifile": "synthetic-file-cache.torrent", "attributes": {"Category": "Preview"},
         "props": {"persistent": "true"}},
        {"type": 3, "file": str(payload / "Synthetic shared directory"), "recursive": 1,
         "props": {"persistent": "false", "tags": "101,102"}},
    ]}))
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", type=Path, help="Create sample data in a new /tmp/biglybtc-* directory and exit")
    args = parser.parse_args()
    if args.prepare:
        try:
            validate_root(args.prepare)
        except ValueError as error:
            parser.error(str(error))
        args.prepare.mkdir(mode=0o700)
        print(prepare(args.prepare))
        return 0
    prefix = Path(__file__).resolve().parent.parent
    executable = prefix / "lib/biglybtc/bin/BiglyBTC"
    if not executable.is_file():
        executable = prefix / "bin/BiglyBTC"
    if not executable.is_file():
        parser.error("The BiglyBTC executable is missing; reinstall the preview package")
    with tempfile.TemporaryDirectory(prefix="biglybtc-preview-", dir="/tmp") as temporary:
        root = Path(temporary)
        profile = prepare(root)
        environment = os.environ.copy()
        for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH"):
            environment.pop(name, None)
        # Keep preview settings/cache separate from other applications too.
        for variable in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
            directory = root / variable.lower()
            directory.mkdir(mode=0o700)
            environment[variable] = str(directory)
        print("BiglyBTC offline preview — generated sample data:", profile, flush=True)
        return subprocess.run([str(executable), "--experimental-local", str(profile), "0"],
                              env=environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
