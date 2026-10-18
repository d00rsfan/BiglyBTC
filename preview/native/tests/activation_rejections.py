#!/usr/bin/env python3
"""Unsafe saved policies must fail before state-cache ownership or payload access."""

import argparse
import hashlib
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from handoff import decode


def encode(value):
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    if isinstance(value, list):
        return b"l" + b"".join(encode(v) for v in value) + b"e"
    return b"d" + b"".join(encode(k) + encode(v) for k, v in value.items()) + b"e"


def digests(root):
    result = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError("Generated rejection fixture unexpectedly contains a symbolic link")
        if path.is_file():
            result[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    args = parser.parse_args()
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        return 77
    work = pathlib.Path("/tmp") / ("biglybtc-policy-rejection-" + uuid.uuid4().hex)
    prepared = work / "prepared.json"
    subprocess.run(["python3", str(ROOT / "migration/tools/multifile_prepare.py"), "--type", "1",
        "--work", str(work), "--output", str(prepared)], check=True, timeout=360)
    reference = json.loads(prepared.read_text())
    original = pathlib.Path(reference["profiles"][0]["profile"])
    source_digests = digests(original.parent)
    cases = [("skipped", {b"file_priorities_c": {b"0": b"0"}}, "Skipped files"),
        ("ambiguous", {b"file_priorities_c": {b"1": b"0", b"2": b"0"}}, "Conflicting original"),
        ("unbounded", {b"file_priorities_c": {b"1": b"2147483647"}}, "outside metadata"),
        ("disabled-download", {b"maxdl": -1}, "Disabled or invalid"),
        ("overflow-upload", {b"maxul": 2147483648}, "Disabled or invalid")]
    results = []
    for name, changes, message in cases:
        profile = work / name / "profile"
        shutil.copytree(original, profile)
        inventory, _ = decode((profile / "downloads.config").read_bytes())
        inventory[b"downloads"][0].update(changes)
        (profile / "downloads.config").write_bytes(encode(inventory))
        before = digests(profile)
        with socket.socket() as owned:
            owned.bind(("127.0.0.1", 0))
            owned.listen(1)
            owned.settimeout(.05)
            result = subprocess.run([args.app, "--experimental-local", str(profile), reference["profiles"][0]["hash"],
                str(owned.getsockname()[1])], text=True, capture_output=True, timeout=30)
            if result.returncode != 1 or message not in result.stderr:
                raise ValueError("Saved policy did not fail at the intended preflight: " + repr((name, result.returncode, result.stderr)))
            try:
                connection, _ = owned.accept()
            except socket.timeout:
                pass
            else:
                connection.close()
                raise ValueError("Rejected saved policy opened a peer connection")
        if digests(profile) != before or digests(original.parent) != source_digests:
            raise ValueError("Rejected saved policy mutated profile/cache/state or original payload")
        results.append({"case": name, "error": result.stderr.strip(), "all_profile_bytes_preserved": True, "peer_connection": False})
    (work / "result.json").write_text(json.dumps({"status": "passed", "source_commit": reference["source_commit"],
        "native_binary_sha256": hashlib.sha256(pathlib.Path(args.app).read_bytes()).hexdigest(), "cases": results}, indent=2) + "\n")
    print("PASS unsafe saved policy rejected before all profile/cache/payload mutations; evidence", work)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
