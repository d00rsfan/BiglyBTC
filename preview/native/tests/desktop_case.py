#!/usr/bin/env python3
"""Run one real desktop same-profile case in a fresh retained generated root."""

import argparse
import pathlib
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", required=True)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--type", required=True)
    parser.add_argument("--action", default="complete")
    args = parser.parse_args()
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        print("SKIP: build the source-pinned Java oracle first")
        return 77
    root = pathlib.Path(__file__).resolve().parents[2]
    work = pathlib.Path("/tmp") / ("biglybtc-desktop-test-" + uuid.uuid4().hex)
    result = subprocess.run(["python3", str(root / "migration/tools/desktop_interop.py"),
        "--test", args.test, "--peer", args.peer, "--type", args.type, "--action", args.action,
        "--work", str(work), "--output", str(work / "result.json")], timeout=540)
    print("Actual desktop/Java evidence:", work)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
