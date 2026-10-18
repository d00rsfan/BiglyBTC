#!/usr/bin/env python3
"""Fresh original multifile profiles for native or Qt same-file CTest cases."""

import argparse
import pathlib
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app")
    parser.add_argument("--gui-test")
    parser.add_argument("--peer", required=True)
    parser.add_argument("--type", required=True)
    parser.add_argument("--crash")
    parser.add_argument("--priorities", action="store_true")
    args = parser.parse_args()
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        return 77
    root = pathlib.Path(__file__).resolve().parents[2]
    work = pathlib.Path("/tmp") / ("biglybtc-multifile-case-" + uuid.uuid4().hex)
    prepared = work / "preparation.json"
    subprocess.run(["python3", str(root / "migration/tools/multifile_prepare.py"), "--type", args.type,
        "--work", str(work), "--output", str(prepared), *(["--priorities"] if args.priorities else [])], check=True, timeout=360)
    command = ["python3", str(root / "migration/tools/multifile_handoff.py"), "--peer", args.peer,
        "--prepared", str(prepared), "--type", args.type, "--output", str(work / "result.json")]
    command += ["--gui-test", args.gui_test] if args.gui_test else ["--app", args.app]
    if args.crash is not None:
        command += ["--crash", args.crash, "--java-first"]
    result = subprocess.run(command, timeout=540)
    print("Multifile same-profile evidence:", work)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
