#!/usr/bin/env python3
"""Compare native policy decoding with the unchanged source-built Java loader."""

import argparse
import pathlib
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from oracle import JAVA, verify_classes
from peer_interop import jvm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    if not pathlib.Path("/tmp/biglybtc-oracle/classes").is_dir():
        return 77
    verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    work = pathlib.Path("/tmp") / ("biglybtc-policy-interop-" + uuid.uuid4().hex)
    work.mkdir()
    classes = work / "classes"
    classes.mkdir()
    subprocess.run([str(JAVA / "javac"), "-J-XX:ActiveProcessorCount=2", "--release", "8", "-cp", "/tmp/biglybtc-oracle/classes",
        "-d", str(classes), *[str(ROOT / "migration/fixtures/java" / name) for name in ("ReferenceFence.java", "FilePolicyOracle.java")]], check=True, timeout=90)
    cases = [b"de", b"d15:file_prioritiesli-1ei-1ei-1eee",
        b"d15:file_prioritiesli0ei1ei-1ei-4ei2147483647ei-2147483648ei4294967296eee",
        b"d15:file_prioritiesli-2ei2147483648ei-2147483649ei9223372036854775807ei-9223372036854775808eee"]
    counts = [3, 3, 7, 5]
    for index, (encoded, count) in enumerate(zip(cases, counts)):
        java = work / str(index)
        java.mkdir()
        with (java / "java.log").open("w") as output:
            result = subprocess.run([*jvm(java, str(classes) + ":/tmp/biglybtc-oracle/classes"), "FilePolicyOracle",
                encoded.hex(), str(count)], cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=60)
        if result.returncode:
            raise ValueError("Original policy oracle failed; see " + str(java / "java.log"))
        expected = [line.removeprefix("POLICY ") for line in (java / "java.log").read_text(errors="replace").splitlines() if line.startswith("POLICY ")]
        actual = subprocess.run([args.driver, encoded.hex(), str(count)], check=True, text=True, capture_output=True, timeout=30).stdout.splitlines()
        if len(expected) != count or actual != expected:
            raise ValueError("Original/native file policy differs: " + repr((expected, actual)))
    print("PASS source-built original signed/legacy file-priority loader differential; evidence", work)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
