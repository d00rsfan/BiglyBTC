#!/usr/bin/env python3
"""Exercise real bounded write failures without changing any parent/system limits."""

import argparse
import hashlib
import json
import pathlib
import resource
import signal
import subprocess
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from oracle import JAVA, verify_classes
from peer_interop import jvm
from profile_tests import ORIGINAL


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", type=pathlib.Path, required=True)
    args = parser.parse_args()
    driver = args.driver.resolve()
    if not str(driver).startswith("/tmp/biglybtc-"):
        raise ValueError("Only a generated native test driver is accepted")
    oracle = verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    parent_limits = (resource.getrlimit(resource.RLIMIT_FSIZE), resource.getrlimit(resource.RLIMIT_CORE))
    work = pathlib.Path("/tmp") / ("biglybtc-profile-write-faults-" + uuid.uuid4().hex)
    work.mkdir()
    classes = work / "classes"
    classes.mkdir()
    sources = [ROOT / "native/tests/ProfileOracle.java", ROOT / "migration/fixtures/java/ReferenceFence.java"]
    subprocess.run([str(JAVA / "javac"), "-J-XX:ActiveProcessorCount=2", "--release", "8", "-cp",
        "/tmp/biglybtc-oracle/classes", "-d", str(classes), *map(str, sources)], check=True, timeout=90)
    cases = []
    java_arguments = []
    for limit in (0, 16):
        for mode in ("reported", "terminated"):
            name = mode + "-" + str(limit)
            profile = work / name
            profile.mkdir()
            primary = profile / "biglybt.config"
            backup = profile / "biglybt.config.bak"
            primary.write_bytes(ORIGINAL)
            backup.write_bytes(ORIGINAL.replace(b"51413", b"51412"))
            before = {p.name: (digest(p), p.stat().st_dev, p.stat().st_ino) for p in (primary, backup)}
            ready = subprocess.run([str(driver), "read", str(profile), primary.name], capture_output=True, timeout=30)
            if ready.returncode or b"PORT 51413" not in ready.stdout:
                raise ValueError("Native preflight cannot read the original fixture")

            def limits():
                # This executes in the freshly forked disposable child only.
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN if mode == "reported" else signal.SIG_DFL)
                resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

            result = subprocess.run([str(driver), "set-port", str(profile), primary.name, "normal"],
                preexec_fn=limits, capture_output=True, timeout=30)
            expected = 2 if mode == "reported" else -signal.SIGXFSZ
            if result.returncode != expected:
                raise ValueError("Native write fault outcome differs: " + repr((name, result.returncode, result.stderr)))
            if mode == "reported" and b"Unable to write complete saving file; original preserved" not in result.stderr:
                raise ValueError("Native did not report the actual saving-file write failure")
            saving = profile / "biglybt.config.saving"
            if mode == "reported":
                if saving.exists():
                    raise ValueError("Failed write retained its owned partial saving file and blocks retry")
            elif not saving.is_file() or saving.stat().st_size != limit:
                raise ValueError("Expected the actual bounded partial saving file after abrupt termination")
            for path in (primary, backup):
                if (digest(path), path.stat().st_dev, path.stat().st_ino) != before[path.name]:
                    raise ValueError("Failed write changed a committed configuration inode/byte")
            reopened = subprocess.run([str(driver), "read", str(profile), primary.name], capture_output=True, timeout=30)
            if reopened.returncode or b"PORT 51413 SOURCE 0" not in reopened.stdout:
                raise ValueError("Native trusted a partial saving file over the intact primary")
            cases.append({"case": name, "child_exit": result.returncode, "file_size_limit": limit,
                "saving_bytes": saving.stat().st_size if saving.exists() else None, "committed_files_sha256_device_inode": before,
                "native_diagnostic": result.stderr.decode(errors="replace"), "profile": str(profile)})
            java_arguments.extend((str(profile), primary.name))
    classpath = str(classes) + ":/tmp/biglybtc-oracle/classes"
    log = work / "java-recovery.log"
    with log.open("w") as output:
        result = subprocess.run([*jvm(work, classpath), "ProfileOracle", *java_arguments], cwd=work,
            stdout=output, stderr=subprocess.STDOUT, timeout=90)
    content = log.read_text(errors="replace")
    if result.returncode or any("JAVA_PORT " + case["case"] + " 51413" not in content for case in cases):
        raise ValueError("Original Java did not reopen intact configurations: " + str(log))
    for case in cases:
        for name, expected in case["committed_files_sha256_device_inode"].items():
            path = pathlib.Path(case["profile"]) / name
            if (digest(path), path.stat().st_dev, path.stat().st_ino) != expected:
                raise ValueError("Original recovery changed an intact committed configuration")
    retry_arguments = []
    for case in cases:
        if not case["case"].startswith("reported-"):
            continue
        profile = pathlib.Path(case["profile"])
        retried = subprocess.run([str(driver), "set-port", str(profile), "biglybt.config", "normal"],
            capture_output=True, timeout=30)
        if retried.returncode:
            raise ValueError("A later successful configuration write is blocked after a reported error")
        primary = profile / "biglybt.config"
        backup = profile / "biglybt.config.bak"
        if primary.read_bytes() != ORIGINAL.replace(b"51413", b"51414") or backup.read_bytes() != ORIGINAL:
            raise ValueError("Retry lost opaque bytes or the previous committed configuration")
        original_primary = case["committed_files_sha256_device_inode"][primary.name]
        if (backup.stat().st_dev, backup.stat().st_ino) != original_primary[1:]:
            raise ValueError("Successful retry did not preserve the previous primary inode as backup")
        case["later_save_succeeded"] = True
        case["retry_primary_sha256"] = digest(primary)
        case["retry_backup_sha256"] = digest(backup)
        retry_arguments.extend((str(profile), primary.name))
    retry_log = work / "java-retry.log"
    with retry_log.open("w") as output:
        result = subprocess.run([*jvm(work, classpath), "ProfileOracle", *retry_arguments], cwd=work,
            stdout=output, stderr=subprocess.STDOUT, timeout=90)
    if result.returncode or any("JAVA_PORT " + case["case"] + " 51414" not in retry_log.read_text(errors="replace")
            for case in cases if case["case"].startswith("reported-")):
        raise ValueError("Original Java did not reopen the successful retried configuration: " + str(retry_log))
    if parent_limits != (resource.getrlimit(resource.RLIMIT_FSIZE), resource.getrlimit(resource.RLIMIT_CORE)):
        raise ValueError("Test leaked resource limits into its parent")
    report = {"status": "passed", "source_commit": oracle["source_commit"],
        "core_class_digest": oracle["core_class_digest"], "driver_sha256": digest(driver),
        "harness_sha256": {str(p.relative_to(ROOT)): digest(p) for p in sources + [pathlib.Path(__file__)]},
        "cases": cases, "parent_limits_unchanged": parent_limits,
        "java_log": str(log), "java_log_sha256": digest(log),
        "java_retry_log": str(retry_log), "java_retry_log_sha256": digest(retry_log),
        "limitations": ["Linux actual RLIMIT_FSIZE/EFBIG and child SIGXFSZ only; no ENOSPC/fsync/rename fault qualification",
            "Abruptly terminated children cannot run sanitizer finalization; reported-error/reopen paths retain normal leak checks",
            "Generated configuration files only; no payload or running user application is involved"]}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS four actual write faults and two successful retries; original bytes/inodes and Java/native reopen preserved; evidence", work)


if __name__ == "__main__":
    main()
