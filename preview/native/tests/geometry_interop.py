#!/usr/bin/env python3
"""Exercise native geometry on the retained source-built Java multifile outputs."""

import argparse
import hashlib
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    record = root / "migration/evidence/P03-03/java-multifile-prepared.json"
    if not record.exists():
        return 77
    reference = json.loads(record.read_text())
    work = pathlib.Path(reference["work"])
    if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-") or reference["status"] != "prepared":
        raise ValueError("Only generated original multifile fixtures are accepted")
    paths = [work / "generation/fixtures" / ("multi-" + str(kind) + ".torrent") for kind in (1, 3, 2)]
    if not all(p.is_file() for p in paths):
        return 77
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    subprocess.run([args.driver, *map(str, paths)], check=True, timeout=30)
    if [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths] != before:
        raise ValueError("Geometry query modified original Java fixture bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
