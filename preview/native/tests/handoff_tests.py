#!/usr/bin/env python3
"""CTest entry point; retain generated files/reports for failure inspection."""

import argparse
import pathlib
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--type", type=int, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        print("SKIP: build the source-pinned Java reference oracle first")
        return 77
    work = pathlib.Path("/tmp") / ("biglybtc-handoff-test-" + uuid.uuid4().hex)
    result = subprocess.run(["python3", str(root / "migration/tools/handoff.py"),
        "--app", args.app, "--seed", args.peer, "--type", str(args.type),
        "--work", str(work), "--output", str(work / "result.json")], timeout=330)
    print("Generated same-profile handoff evidence:", work)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
