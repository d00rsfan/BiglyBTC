#!/usr/bin/env python3
"""Exercise existing native state-writer crash/conflict controls on owned fixtures."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


def encode(value):
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, bytes):
        return str(len(value)).encode() + b":" + value
    return b"d" + b"".join(encode(k) + encode(v) for k, v in sorted(value.items())) + b"e"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=pathlib.Path, required=True)
    args = parser.parse_args()
    driver = args.driver.resolve(strict=True)
    # Same deterministic info/resume shape as existing local_rating_tests.cpp.
    info = {b"length": 3, b"name": b"fixture.bin", b"piece length": 16384, b"pieces": b"x" * 20}
    resume = {b"data": {b"blocks": {}, b"resume data": b"\x01\x00\x02", b"valid": 1}}
    state = encode({b"attributes": {b"native.fixture": 2}, b"future-root": b"untouched", b"info": info, b"resume": resume})
    identity = hashlib.sha1(encode(info)).hexdigest().upper()
    rows = []

    def run(profile, *command, expected=0):
        result = subprocess.run([str(driver), str(profile), identity, *map(str, command)], capture_output=True, text=True, timeout=30)
        if result.stderr:
            sys.stderr.write(result.stderr)
        assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
        rows.append({"command": list(map(str, command)), "exit": result.returncode, "stdout": result.stdout.strip()})

    with tempfile.TemporaryDirectory(prefix="biglybtc-state-writer-guards-") as directory:
        root = pathlib.Path(directory)

        def fresh(name):
            profile = root / name
            (profile / "active").mkdir(parents=True, mode=0o700)
            profile.chmod(0o700)
            path = profile / "active" / (identity + ".dat")
            path.write_bytes(state)
            # beginUpdate intentionally marks resume data for recheck. Compare
            # crash boundaries with the actual prepared native state.
            run(profile, "begin")
            prepared = path.read_bytes()
            assert encode(info) in prepared and encode(b"untouched") in prepared
            assert prepared.count(b"14:native.fixturei2e") == 1
            return profile, path, prepared

        for stage in ("normal", "temp", "backup", "primary"):
            profile, path, prepared = fresh(stage)
            run(profile, "edit", stage, expected=0 if stage == "normal" else 66)
            run(profile, "load")
            current = path.read_bytes()
            assert encode(info) in current and encode(b"untouched") in current
            if stage in ("normal", "primary"):
                assert current == prepared.replace(b"14:native.fixturei2e", b"14:native.fixturei7e")
            else:
                assert current == prepared
        profile, path, prepared = fresh("stale")
        run(profile, "stale")
        assert path.read_bytes() == b"de", "A stale save overwrote the foreign primary"
        profile, path, prepared = fresh("other-profile")
        other, other_path, other_prepared = fresh("other-target")
        run(profile, "other-profile", other)
        assert path.read_bytes() == prepared == other_path.read_bytes() == other_prepared
        profile, path, prepared = fresh("immutable")
        run(profile, "immutable")
        assert path.read_bytes() == prepared
    print("PASS native state writer guards " + json.dumps({"cases": 7, "driver_sha256": hashlib.sha256(driver.read_bytes()).hexdigest(), "fixture_sha256": hashlib.sha256(state).hexdigest(), "commands": rows}, sort_keys=True))


if __name__ == "__main__":
    main()
