#!/usr/bin/env python3
"""No-gap native state saves, authoritative cache transfer and real Java reads."""

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from oracle import JAVA, verify_classes
from peer_interop import jvm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    record = ROOT / "migration/evidence/P00-04/java-native-v1.json"
    if not record.exists():
        print("SKIP: generate the guarded Java peer reference first")
        return 77
    reference = json.loads(record.read_text())
    original_root = pathlib.Path(reference["java_log"]).parent.resolve()
    if original_root.parent != pathlib.Path("/tmp") or not original_root.name.startswith("biglybtc-peer-"):
        raise ValueError("Only our generated original profile is accepted")
    original = original_root / "java/profile/active"
    if not original.exists():
        print("SKIP: generated Java state fixture is no longer retained")
        return 77
    name = next(name for name in reference["profile_files_sha256"]
        if name.startswith("active/") and name.endswith(".dat") and name != "active/cache.dat")
    name = pathlib.PurePosixPath(name).name
    identity = name[:-4]
    source_state = (original / name).read_bytes()
    source_cache = (original / "cache.dat").read_bytes()
    for key, value in [("active/" + name, source_state), ("active/cache.dat", source_cache)]:
        if hashlib.sha256(value).hexdigest() != reference["profile_files_sha256"][key]:
            raise ValueError("Original generated state/cache differs from retained evidence")
    verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    with tempfile.TemporaryDirectory(prefix="biglybtc-torrent-state-") as directory:
        work = pathlib.Path(directory)
        classes = work / "classes"
        classes.mkdir()
        sources = [ROOT / "migration/fixtures/java" / item for item in
            ("ReferenceFence.java", "StateStoreOracle.java", "CacheFixtureOracle.java")]
        subprocess.run(list(map(str, [JAVA / "javac", "-J-XX:ActiveProcessorCount=2", "--release", "8",
            "-cp", "/tmp/biglybtc-oracle/classes", "-d", classes, *sources])), check=True, timeout=60)
        java = jvm(work, str(classes) + ":/tmp/biglybtc-oracle/classes")

        def run(command, expected=0, timeout=30):
            result = subprocess.run(list(map(str, command)), cwd=work, capture_output=True, timeout=timeout)
            if result.returncode != expected:
                raise ValueError("Unexpected test exit " + str(result.returncode) + ": "
                    + result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
            return result.stdout.decode(errors="replace")

        def profile(label):
            path = work / "cases" / label
            (path / "active").mkdir(parents=True)
            (path / "active" / name).write_bytes(source_state)
            (path / "active/cache.dat").write_bytes(source_cache)
            return path

        java_cases = []
        for stage in ("normal", "temp", "backup", "primary"):
            path = profile(stage)
            run([args.driver, path, identity, "edit", stage], expected=0 if stage == "normal" else 66)
            main_state = path / "active" / name
            if not main_state.exists():
                raise ValueError("A crash left the original Java primary missing")
            run([args.driver, path, identity, "load"])
            expected = 7 if stage in ("normal", "primary") else -1
            java_cases.extend([main_state, identity, expected, stage, -1])
            if stage == "normal":
                fallback = profile("fallback")
                shutil.copyfile(path / "active" / (name + ".bak"), fallback / "active" / (name + ".bak"))
                (fallback / "active" / name).write_bytes(b"broken")
                loaded = run([args.driver, fallback, identity, "load"])
                if "LOADED 1" not in loaded:
                    raise ValueError("Native reader did not recover original-compatible backup")
                java_cases.extend([fallback / "active" / name, identity, 7, "fallback", -1])

        mixed = profile("mixed")
        run([*java, "CacheFixtureOracle", "prepare", mixed / "active/cache.dat"], timeout=60)
        run([args.driver, mixed, identity, "begin"])
        if "CACHE_VERIFIED" not in run([*java, "CacheFixtureOracle", "verify", mixed / "active/cache.dat"], timeout=60):
            raise ValueError("Original Java cache verifier did not run")
        java_cases.extend([mixed / "active" / name, identity, -1, "mixed", 42])

        corrupt = profile("corrupt-cache")
        bad = b"not a complete gzip cache"
        (corrupt / "active/cache.dat").write_bytes(bad)
        run([args.driver, corrupt, identity, "begin"])
        if (corrupt / "active/cache.dat").exists() or (corrupt / "active/cache.dat.bak").read_bytes() != bad:
            raise ValueError("Corrupt non-authoritative cache was not retained safely")
        java_cases.extend([corrupt / "active" / name, identity, -1, "corrupt-cache", -1])

        immutable = profile("immutable")
        run([args.driver, immutable, identity, "immutable"])
        stale = profile("stale")
        run([args.driver, stale, identity, "stale"])
        if (stale / "active" / name).read_bytes() != b"de":
            raise ValueError("Stale state save overwrote a changed primary")
        other = profile("other")
        before = (other / "active" / name).read_bytes()
        run([args.driver, immutable, identity, "other-profile", other])
        if (other / "active" / name).read_bytes() != before:
            raise ValueError("State was written into another profile")

        unsafe = profile("symlink")
        protected = work / "protected"
        protected.write_bytes(b"protected")
        (unsafe / "active" / (name + ".saving")).symlink_to(protected)
        run([args.driver, unsafe, identity, "begin"], expected=2)
        if protected.read_bytes() != b"protected" or (unsafe / "active" / name).read_bytes() != source_state:
            raise ValueError("Unsafe saving entry changed original/linked data")
        output = run([*java, "StateStoreOracle", *java_cases], timeout=60)
        for label in ("normal", "temp", "backup", "primary", "fallback", "mixed", "corrupt-cache"):
            if "STATE_VERIFIED " + label not in output:
                raise ValueError("Missing original Java state checkpoint: " + label)
        if (original / name).read_bytes() != source_state or (original / "cache.dat").read_bytes() != source_cache:
            raise ValueError("Original generated fixture was modified")
        print("PASS original Java reads every native save/crash/backup/cache checkpoint")
        print("PASS cache hydration, unrelated/unknown data, stale/cross-profile/symlink protection and immutable info")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
