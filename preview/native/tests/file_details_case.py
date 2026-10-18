#!/usr/bin/env python3
"""Read original file geometry/policy in Qt with unavailable payloads and stale raw names."""

import argparse
import gzip
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from activation_rejections import encode, digests
from handoff import decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", required=True)
    parser.add_argument("--type", required=True, choices=("1", "2", "3"))
    parser.add_argument("--paused", action="store_true")
    args = parser.parse_args()
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        return 77
    work = pathlib.Path("/tmp") / ("biglybtc-files-view-" + uuid.uuid4().hex)
    prepared = work / "preparation.json"
    subprocess.run(["python3", str(ROOT / "migration/tools/multifile_prepare.py"), "--type", args.type,
        "--priorities", "--work", str(work), "--output", str(prepared)], check=True, timeout=360)
    reference = json.loads(prepared.read_text())
    case = reference["profiles"][0]
    profile = pathlib.Path(case["profile"])
    java = profile.parent
    identity = bytes.fromhex(case["hash"])
    # This explicit fixture overlay exercises the real cache-ownership contract:
    # a cached display name can be newer than the raw torrent-state attributes.
    cached_name = "Original cached display — שب"
    cache_path = profile / "active/cache.dat"
    cache, _ = decode(gzip.decompress(cache_path.read_bytes()))
    matches = [item for item in cache[b"state"] if item[b"hash"] == identity]
    if len(matches) != 1:
        raise ValueError("Original cache fixture identity is ambiguous")
    matches[0][b"attributes"][b"displayname"] = cached_name.encode()
    cache_path.write_bytes(gzip.compress(encode(cache), mtime=0))
    state_path = profile / "active" / (case["hash"] + ".dat")
    original = state_path.read_bytes()
    compressed = original.startswith(b"\x1f\x8b")
    state, _ = decode(gzip.decompress(original) if compressed else original)
    state[b"attributes"][b"displayname"] = b"stale raw state name"
    changed = encode(state)
    state_path.write_bytes(gzip.compress(changed, mtime=0) if compressed else changed)
    if args.paused:
        inventory_path = profile / "downloads.config"
        inventory, _ = decode(inventory_path.read_bytes())
        inventory[b"downloads"][0][b"state"] = 70
        inventory[b"downloads"][0][b"forceStart"] = 0
        inventory[b"pause_data"] = [{b"hash": identity, b"force": 0, b"owner": 1}]
        inventory_path.write_bytes(encode(inventory))
    before = digests(profile)
    payload = java / "payload"
    content_before = digests(payload)
    offline = java / "payload-offline"
    payload.rename(offline)
    for name in ("qt-cache", "qt-config", "qt-runtime"):
        (java / name).mkdir(mode=0o700)
    capture = java / "BiglyBTC-files.png"
    environment = os.environ.copy()
    environment.update(QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", XDG_CACHE_HOME=str(java / "qt-cache"),
        XDG_CONFIG_HOME=str(java / "qt-config"), XDG_RUNTIME_DIR=str(java / "qt-runtime"),
        BIGLYBTC_TEST_PROFILE=str(profile), BIGLYBTC_TEST_PADDED="0" if args.type == "1" else "1",
        BIGLYBTC_TEST_PAUSED="1" if args.paused else "0", BIGLYBTC_TEST_CACHED_NAME=cached_name,
        BIGLYBTC_TEST_CAPTURE=str(capture))
    log = java / "native-files.log"
    try:
        with log.open("w") as output:
            result = subprocess.run([args.test, "readOnlyFileDetails"], env=environment, stdout=output,
                stderr=subprocess.STDOUT, timeout=180)
    finally:
        offline.rename(payload)
    if result.returncode or "READ_ONLY_ORIGINAL_FILES_VERIFIED" not in log.read_text(errors="replace"):
        raise ValueError("Read-only original Qt file details failed; see " + str(log))
    if digests(profile) != before or digests(payload) != content_before:
        raise ValueError("Read-only file details changed original profile/cache/payload bytes")
    report = {"status": "passed", "source_commit": reference["source_commit"],
        "core_class_digest": reference["core_class_digest"], "torrent_type": int(args.type),
        "native_test_sha256": hashlib.sha256(pathlib.Path(args.test).read_bytes()).hexdigest(),
        "profile": str(profile), "profile_sha256_unchanged": before, "payload_sha256_unchanged": content_before,
        "fixture_overlays": ["Authoritative cached display name differs from stale raw state name",
            "Saved stopped state plus original general-owner pause entry" if args.paused else "No pause overlay"],
        "assertions": ["Original indices/Unicode/empty files/signed priorities", "Virtual padding hidden/toggled",
            "Cached display name remains authoritative", "Refresh does not mutate metadata or cache",
            "Payload path unavailable throughout native query/view", "No permanent JVM dependency"],
        "log": str(log), "screenshot": str(capture),
        "limitations": ["Generated read-only Linux offscreen file view; editing/platform/full-view parity remains open"]}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS original file metadata/policy in Qt without payload access; evidence", work)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
