#!/usr/bin/env python3
"""Read actual source-built Java cache/inventory without changing profile bytes."""

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    profiles = []
    for variant in ("v1", "v2", "hybrid"):
        record = root / ("migration/evidence/P00-04/java-native-" + variant + ".json")
        if not record.exists():
            return 77
        reference = json.loads(record.read_text())
        work = pathlib.Path(reference["java_log"]).parent.resolve()
        if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-peer-"):
            raise ValueError("Only retained generated Java fixture roots are accepted")
        profile = work / "java/profile"
        if not profile.exists():
            return 77
        before = {str(p.relative_to(profile)): hashlib.sha256(p.read_bytes()).hexdigest() for p in profile.rglob("*") if p.is_file()}
        if before != reference["profile_files_sha256"]:
            raise ValueError("Retained Java fixture files changed")
        result = subprocess.run([args.driver, str(profile)], capture_output=True, text=True, check=True, timeout=30)
        lines = result.stdout.splitlines()
        if lines[0] != "VIEW 1 0 0 0" or len(lines) != 2:
            raise ValueError("Actual original cache view did not contain exactly one valid metadata entry: " + result.stdout)
        item = lines[1].split()
        if item[0] != "ITEM" or item[2] != "131072" or item[4] != "1" or item[6] != b"generated.bin".hex().upper():
            raise ValueError("Original cached size/count/name differs from native view: " + result.stdout)
        after = {str(p.relative_to(profile)): hashlib.sha256(p.read_bytes()).hexdigest() for p in profile.rglob("*") if p.is_file()}
        if after != before:
            raise ValueError("Read-only original library/cache query changed profile bytes")
        profiles.append(profile)
    with tempfile.TemporaryDirectory(prefix="biglybtc-library-view-") as directory:
        work = pathlib.Path(directory)
        profile = work / "profile"
        (profile / "active").mkdir(parents=True)
        shutil.copyfile(profiles[0] / "downloads.config", profile / "downloads.config")

        def read():
            return subprocess.run([args.driver, str(profile)], capture_output=True, text=True, check=True, timeout=30).stdout

        if not read().startswith("VIEW 1 0 0 0\n"):
            raise ValueError("Missing optional metadata cache must keep the saved library available")
        (profile / "active/cache.dat").write_bytes(b"broken")
        if not read().startswith("VIEW 1 0 0 1\n") or (profile / "active/cache.dat").read_bytes() != b"broken":
            raise ValueError("Bad cache was modified or hid the saved library")
        (profile / "active/cache.dat").unlink()
        protected = work / "protected"
        protected.write_bytes(b"protected")
        (profile / "active/cache.dat").symlink_to(protected)
        if not read().startswith("VIEW 1 0 0 1\n") or protected.read_bytes() != b"protected":
            raise ValueError("Read-only cache query followed or changed a symbolic target")
    print("PASS actual Java v1/v2/hybrid cached names/sizes and unchanged profile bytes")
    print("PASS missing/corrupt/symbolic cache keeps inventory readable without changing data")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
