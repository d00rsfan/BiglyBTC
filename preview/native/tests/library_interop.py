#!/usr/bin/env python3
"""Inspect only retained generated Java profiles and verify their bytes stay intact."""

import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in root.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    manifest = json.loads((ROOT / "migration/evidence/P00-03/formats.json").read_text())
    for variant in ("v1", "v2", "hybrid"):
        record = ROOT / ("migration/evidence/P00-04/java-native-" + variant + ".json")
        if not record.exists():
            print("SKIP: generate guarded Java peer reference profiles with migration/tools/peer_interop.py")
            return 77
        report = json.loads(record.read_text())
        if report["source_commit"] != manifest["source_commit"] or report["core_class_digest"] != manifest["core_class_digest"]:
            raise ValueError("Operational Java fixture has a different source-built core identity")
        work = pathlib.Path(report["java_log"]).parent.resolve()
        if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-peer-"):
            raise ValueError("Reference is not our generated test work directory")
        profile = work / "java/profile"
        if not profile.exists():
            print("SKIP: retained generated Java profile is unavailable:", variant)
            return 77
        before = hashes(profile)
        if before != report["profile_files_sha256"]:
            raise ValueError("Original generated Java profile differs from its retained evidence")
        state_names = [name for name in before if name.startswith("active/") and name.endswith(".dat") and name != "active/cache.dat"]
        if len(state_names) != 1:
            raise ValueError("Expected exactly one original Java torrent-state fixture")
        expected_hash = pathlib.PurePosixPath(state_names[0]).stem
        result = subprocess.run([args.driver, str(profile)], capture_output=True, timeout=30)
        lines = result.stdout.decode().splitlines()
        if result.returncode or len(lines) != 2 or lines[0] != "LIBRARY 1 0 0 0":
            raise ValueError("Native inventory did not match original Java profile: " + result.stderr.decode(errors="replace"))
        row = lines[1].split()
        if row[:7] != ["ITEM", "0", expected_hash, "75", "75", "0", "1"] or int(row[7]) < 131072 or int(row[8]) < 0 or row[9] != "131072":
            raise ValueError("Native inventory lost original hash, startup/force/persistence state or counters")
        if hashes(profile) != before:
            raise ValueError("Read-only native inventory changed a generated original profile")
        print("PASS original Java", variant, "inventory, identities, counters and all profile bytes retained")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
