#!/usr/bin/env python3
"""Native profile safety and real Java FileChannel interoperation, offline."""

import argparse
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from oracle import verify_classes
JAVA = pathlib.Path("/usr/lib/jvm/java-17-openjdk-amd64/bin")
ORACLE_CLASSES = pathlib.Path("/tmp/biglybtc-oracle/classes")
ORIGINAL = b"d15:TCP.Listen.Porti51413e7:unknownld1:x4:\x00\xff\x80\x7feee"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True)
    args = parser.parse_args()
    checks = []
    with tempfile.TemporaryDirectory(prefix="biglybtc-profile-tests-") as directory:
        work = pathlib.Path(directory)
        subprocess.run([str(JAVA / "javac"), "-J-XX:ActiveProcessorCount=2", "--release", "8", "-d", str(work),
            str(ROOT / "native/tests/LockOracle.java")], check=True, timeout=60)
        if not (ORACLE_CLASSES / "com/biglybt/core/util/FileUtil.class").is_file():
            raise AssertionError("Build the frozen core first: python3 migration/tools/oracle.py --work /tmp/biglybtc-oracle")
        verify_classes(ORACLE_CLASSES)
        subprocess.run([str(JAVA / "javac"), "-J-XX:ActiveProcessorCount=2", "--release", "8", "-cp", str(ORACLE_CLASSES),
            "-d", str(work), str(ROOT / "native/tests/ProfileOracle.java")], check=True, timeout=60)

        def driver(command, profile, *rest, expected=0):
            result = subprocess.run([args.driver, command, str(profile), *map(str, rest)], capture_output=True, timeout=20)
            if result.returncode != expected:
                raise AssertionError(f"{command}: exit {result.returncode}; {result.stderr.decode(errors='replace')}")
            return result.stdout.decode()

        def java(profile, *rest, expected=0):
            result = subprocess.run([str(JAVA / "java"), "-XX:ActiveProcessorCount=2", "-cp", str(work), "LockOracle", str(profile), *map(str, rest)],
                capture_output=True, timeout=15)
            if result.returncode != expected:
                raise AssertionError(f"Java lock exit {result.returncode}: {result.stderr.decode(errors='replace')}")

        def ready(path, process):
            deadline = time.monotonic() + 15
            while not path.exists() and time.monotonic() < deadline:
                if process.poll() is not None:
                    raise AssertionError("Lock holder exited before readiness")
                time.sleep(.02)
            if not path.exists():
                raise AssertionError("Lock readiness timeout")

        profile = work / "locks"
        profile.mkdir()
        for command in ("hold-lock", "duplicate-lock"):
            marker = work / command
            process = subprocess.Popen([args.driver, command, str(profile), str(marker)])
            try:
                ready(marker, process)
                java(profile, expected=3)
                driver("try-lock", profile, expected=3)
            finally:
                process.terminate()
                process.wait(timeout=5)
            java(profile)
            checks.append(command + " conflicts with Java and native; death releases lock")
        marker = work / "java-ready"
        process = subprocess.Popen([str(JAVA / "java"), "-XX:ActiveProcessorCount=2", "-cp", str(work), "LockOracle", str(profile), str(marker)], stdout=subprocess.DEVNULL)
        try:
            ready(marker, process)
            driver("try-lock", profile, expected=3)
        finally:
            process.terminate()
            process.wait(timeout=5)
        driver("try-lock", profile)
        checks.append("Java lock excludes native; release permits native")

        java_cases = []
        expected_java = []
        for stage in ("normal", "temp", "backup", "primary"):
            profile = work / stage
            profile.mkdir()
            source = profile / "biglybt.config"
            source.write_bytes(ORIGINAL)
            driver("set-port", profile, source.name, stage, expected=0 if stage == "normal" else 66)
            restored = driver("read", profile, source.name)
            expected_port = "51413" if stage == "temp" else "51414"
            if "PORT " + expected_port not in restored:
                raise AssertionError("Unexpected recovery result after " + stage + ": " + restored)
            if stage == "normal":
                if source.read_bytes() != ORIGINAL.replace(b"51413", b"51414"):
                    raise AssertionError("Unknown bytes changed")
                if (profile / "biglybt.config.bak").read_bytes() != ORIGINAL:
                    raise AssertionError("Backup does not retain prior bytes")
            java_cases.extend([str(profile), source.name])
            expected_java.append("JAVA_PORT " + stage + " " + expected_port)
            checks.append("compatible save/recovery at " + stage)

        # One source-built JVM covers all cases, avoiding repeated legacy core
        # initialization and reducing interference with the user's running app.
        result = subprocess.run([str(JAVA / "java"), "-XX:ActiveProcessorCount=2", "--add-opens=java.base/java.net=ALL-UNNAMED",
            "-Dazureus.config.path=" + str(work / "oracle-settings"), "-cp", str(work) + ":" + str(ORACLE_CLASSES),
            "ProfileOracle", *java_cases], capture_output=True, timeout=30)
        output = result.stdout.decode(errors="replace").splitlines()
        if result.returncode or any(line not in output for line in expected_java):
            raise AssertionError("Java recovery did not match: " + result.stderr.decode(errors="replace"))

        profile = work / "safety"
        profile.mkdir()
        source = profile / "biglybt.config"
        source.write_bytes(ORIGINAL)
        driver("read", profile, "../biglybt.config", expected=2)
        driver("conflict", profile, source.name)
        if source.read_bytes() != b"d3:newi1ee":
            raise AssertionError("Stale save overwrote changed input")
        source.write_bytes(ORIGINAL)
        outside = work / "protected.config"
        outside.write_bytes(b"protected")
        (profile / "biglybt.config.saving").symlink_to(outside)
        driver("set-port", profile, source.name, "normal", expected=2)
        if source.read_bytes() != ORIGINAL or outside.read_bytes() != b"protected":
            raise AssertionError("Existing recovery/symlink target modified")
        checks.append("traversal, stale input, exclusive saving and symlink protection")
        for check in checks:
            print("PASS", check)


if __name__ == "__main__":
    main()
