#!/usr/bin/env python3
"""Prove cleanup preserves a different inode installed at the saving pathname."""

import hashlib
import json
import os
import pathlib
import subprocess
import tempfile

from profile_tests import ORIGINAL

root = pathlib.Path(__file__).resolve().parents[2]
work = pathlib.Path(tempfile.mkdtemp(prefix="biglybtc-replaced-saving-", dir="/tmp"))
profile = work / "profile"
profile.mkdir()
primary = profile / "biglybt.config"
backup = profile / "biglybt.config.bak"
primary.write_bytes(ORIGINAL)
backup.write_bytes(ORIGINAL.replace(b"51413", b"51412"))
before = {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_dev, p.stat().st_ino)
    for p in (primary, backup)}
shim = work / "owned-write-fault.so"
source = root / "native/tests/replaced_saving_fault.cpp"
subprocess.run(["/usr/bin/g++-16", "-std=c++23", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra",
    "-Wpedantic", "-Werror", str(source), "-o", str(shim)], check=True, timeout=60)
driver = pathlib.Path("/tmp/biglybtc-desktop-gcc/tests/biglybtc-profile-driver")
capsule_path = root / "migration/evidence/P02-04/gcc-write-retry-build-capsule.json"
capsule = json.loads(capsule_path.read_text())
driver_hash = hashlib.sha256(driver.read_bytes()).hexdigest()
assert capsule["binary_sha256"]["tests/biglybtc-profile-driver"] == driver_hash
saving = profile / "biglybt.config.saving"
environment = os.environ.copy()
environment.update(LD_PRELOAD=str(shim), BIGLYBTC_FAULT_SAVING=str(saving))
result = subprocess.run([str(driver), "set-port", str(profile), primary.name, "normal"],
    env=environment, capture_output=True, timeout=30)
assert result.returncode == 2 and b"Unable to write complete saving file" in result.stderr, result
assert saving.read_bytes() == b"FOREIGN RECOVERY BYTES\n", "Cleanup removed/replaced the foreign inode"
retained = pathlib.Path(str(saving) + ".owned-partial")
assert retained.read_bytes() == ORIGINAL.replace(b"51413", b"51414")[:16]
for path in (primary, backup):
    assert (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_dev, path.stat().st_ino) == before[path.name]
reopened = subprocess.run([str(driver), "read", str(profile), primary.name], capture_output=True, timeout=30)
assert reopened.returncode == 0 and b"PORT 51413 SOURCE 0" in reopened.stdout
report = {"status": "passed", "native_implementation_commit": capsule["implementation_commit"],
    "native_driver_sha256": driver_hash, "shim_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "harness_sha256": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
    "shim_sha256": hashlib.sha256(shim.read_bytes()).hexdigest(), "work": str(work),
    "committed_files_sha256_device_inode": before,
    "assertions": ["Actual16-byte write then pathname replacement before the reported error",
        "Foreign recovery bytes survive inode-mismatch rollback guard", "Original primary/backup bytes/inodes intact",
        "Native reopens original settings", "LD_PRELOAD and fault pathname exist only in the disposable child environment"],
    "limitations": ["Injected ENOSPC in a test interposer, not a real full disk",
        "GCC Linux control-path test only; broader uncooperative-writer races/filesystem fault matrix remain open"]}
(work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
print("PASS replaced recovery inode preserved during failed-write rollback; evidence", work)
