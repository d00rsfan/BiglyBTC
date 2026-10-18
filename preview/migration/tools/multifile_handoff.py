#!/usr/bin/env python3
"""Continue the SAME original multifile profile and reopen it in original Java."""

import argparse
import json
import os
import pathlib
import subprocess
import time
import uuid

from handoff import decode
from oracle import verify_classes
from peer_interop import jvm, unused_port, owned_peer_port
from sources import ROOT, save, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=pathlib.Path)
    parser.add_argument("--gui-test", type=pathlib.Path)
    parser.add_argument("--peer", type=pathlib.Path, required=True)
    parser.add_argument("--prepared", type=pathlib.Path, required=True)
    parser.add_argument("--type", type=int, choices=(1, 2, 3), required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--crash", type=int, choices=range(6))
    parser.add_argument("--java-first", action="store_true")
    args = parser.parse_args()
    if bool(args.app) == bool(args.gui_test):
        parser.error("Choose exactly one of --app or --gui-test")
    if args.gui_test and args.crash is not None:
        parser.error("GUI crash injection is not part of this test entry point")
    if args.java_first and (args.crash is None or args.crash < 2):
        parser.error("This Java-first multifile case currently covers post-flush boundaries only")
    reference = json.loads(args.prepared.read_text())
    if reference["status"] != "prepared":
        raise ValueError("Original multifile preparation did not finish")
    work = pathlib.Path(reference["work"])
    if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-"):
        raise ValueError("Only retained generated multifile roots are accepted")
    case = next(item for item in reference["profiles"] if item["torrent_type"] == args.type)
    profile = pathlib.Path(case["profile"])
    if profile != work / ("java-" + str(args.type)) / "profile":
        raise ValueError("Original multifile profile identity differs")
    for name, digest in case["profile_sha256"].items():
        if sha256((profile / name).read_bytes()) != digest:
            raise ValueError("Retained original multifile profile changed before first handoff")
    oracle = verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    java = profile.parent
    data_root = java / "payload/multi-fixture"
    expected_root = work / "generation/fixtures/expected/multi-fixture"
    identity = case["hash"]
    state_path = profile / "active" / (identity + ".dat")
    original, spans = decode(state_path.read_bytes())
    initial_inventory, _ = decode((profile / "downloads.config").read_bytes())
    config = (profile / "biglybt.config").read_bytes()
    original_files = {}
    for expected in expected_root.rglob("*"):
        if not expected.is_file():
            continue
        relative = expected.relative_to(expected_root)
        actual = data_root / relative
        if not actual.exists():
            actual = actual.with_name(actual.name + ".az!")
        original_files[str(relative)] = (actual.stat().st_dev, actual.stat().st_ino)
    classpath = str(work / "classes") + ":/tmp/biglybtc-oracle/classes"

    def reopen(label):
        port = unused_port()
        log = java / (label + ".log")
        with log.open("w") as output:
            result = subprocess.run([*jvm(java, classpath, (port,)), "MultiEngineOracle", str(port), "0",
                str(java / "input.torrent"), str(expected_root), "reopen"], cwd=java, stdout=output,
                stderr=subprocess.STDOUT, timeout=180)
        text = log.read_text(errors="replace")
        if result.returncode or "MULTI_VERIFIED" not in text or "MULTI_STATE_PERSISTED" not in text:
            raise ValueError("Original Java multifile reopen failed; see " + str(log))
        return log

    seed_root = work / ("native-peer-" + str(args.type) + '-' + uuid.uuid4().hex)
    started = time.monotonic()
    logs = []
    with (java / "peer.log").open("w") as log:
        peer = subprocess.Popen([str(args.peer), "--seed", str(java / "input.torrent"), str(expected_root), str(seed_root)],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            port = owned_peer_port(seed_root / "ready", peer)
            env = None
            command = [str(args.app), "--experimental-local", str(profile), identity, str(port)]
            if args.gui_test:
                for name in ("qt-cache", "qt-config", "qt-runtime"):
                    (java / name).mkdir(mode=0o700)
                env = os.environ.copy()
                env.update(QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1", XDG_CACHE_HOME=str(java / "qt-cache"),
                    XDG_CONFIG_HOME=str(java / "qt-config"), XDG_RUNTIME_DIR=str(java / "qt-runtime"),
                    BIGLYBTC_TEST_PROFILE=str(profile), BIGLYBTC_TEST_PEER_PORT=str(port), BIGLYBTC_TEST_ACTION="complete",
                    BIGLYBTC_TEST_MULTIFILE="1", BIGLYBTC_TEST_EXPECTED=str(expected_root),
                    BIGLYBTC_TEST_BYTES=str(131072 if args.type == 1 else 147456),
                    BIGLYBTC_TEST_CAPTURE=str(java / "BiglyBTC-multifile.png"))
                command = [str(args.gui_test), "realProfile"]
            first = java / "native-multifile.log"
            with first.open("w") as output:
                result = subprocess.run(command + ([] if args.crash is None else ["--crash", str(args.crash)]),
                    env=env, stdout=output, stderr=subprocess.STDOUT, timeout=150)
            logs.append(first)
            if result.returncode != (0 if args.crash is None else 66):
                raise ValueError("Native same-profile multifile continuation failed; see " + str(first))
            if args.gui_test and "DESKTOP_PROFILE_VERIFIED" not in first.read_text(errors="replace"):
                raise ValueError("Actual multifile desktop case did not run")
            if (profile / "biglybt.config").read_bytes() != config:
                raise ValueError("Native multifile continuation changed global settings")
            if args.crash is not None:
                if args.java_first:
                    logs.append(reopen("interrupted-java"))
                    config = (profile / "biglybt.config").read_bytes()
                recovery = java / "native-multifile-recovery.log"
                with recovery.open("w") as output:
                    result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=150)
                logs.append(recovery)
                if result.returncode:
                    raise ValueError("Native multifile recovery failed; see " + str(recovery))
        finally:
            if seed_root.exists():
                (seed_root / "stop").touch()
            try:
                peer.wait(timeout=15)
            except subprocess.TimeoutExpired:
                peer.terminate()  # Exact generated peer Popen owned by this fixture.
                peer.wait(timeout=10)
    if (profile / "biglybt.config").read_bytes() != config:
        raise ValueError("Native multifile recovery changed global settings")
    native, native_spans = decode(state_path.read_bytes())
    native_inventory, _ = decode((profile / "downloads.config").read_bytes())
    for key in (b"file_priorities", b"file_priorities_c", b"maxdl", b"maxul"):
        if native_inventory[b"downloads"][0].get(key) != initial_inventory[b"downloads"][0].get(key):
            raise ValueError("Native continuation changed saved original file/rate policy: " + key.decode())
    if native_spans[(b"info",)] != spans[(b"info",)] or native[b"unknown.future.plugin"] != original[b"unknown.future.plugin"]:
        raise ValueError("Native multifile state changed info or unknown plugin bytes")
    for relative, inode in original_files.items():
        actual = data_root / relative
        if actual.read_bytes() != (expected_root / relative).read_bytes() or (actual.stat().st_dev, actual.stat().st_ino) != inode:
            raise ValueError("Native multifile bytes/identity differ: " + relative)
        if actual.with_name(actual.name + ".az!").exists():
            raise ValueError("Native multifile completion left an original incomplete alias")
    if (data_root / ".pad").exists():
        raise ValueError("Native created physical files for original virtual padding")
    logs.append(reopen("final-java"))
    original_policy = [line for line in case["engine_observations"] if line.startswith(("MULTI_POLICY ", "MULTI_LIMITS "))]
    if original_policy:
        reopened_policy = [line for line in logs[-1].read_text(errors="replace").splitlines() if line.startswith(("MULTI_POLICY ", "MULTI_LIMITS "))]
        if reopened_policy != original_policy:
            raise ValueError("Original Java reopened different file priorities or torrent rate limits")
    save(args.output, {"status": "passed", "source_commit": oracle["source_commit"],
        "core_class_digest": oracle["core_class_digest"], "torrent_type": args.type, "profile": str(profile),
        "same_profile_at_every_stage": True, "crash_stage": args.crash, "java_before_recovery": args.java_first,
        "native_binary_sha256": sha256((args.gui_test or args.app).read_bytes()),
        "native_binary_kind": "Qt component integration test" if args.gui_test else "native headless executable",
        "original_policy_preserved": True,
        "custom_priorities": reference.get("custom_priorities", False),
        "peer_binary_sha256": sha256(args.peer.read_bytes()),
        "physical_files": {name: sha256((data_root / name).read_bytes()) for name in original_files},
        "profile_after_java_sha256": {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()},
        "logs": [str(p) for p in logs], "elapsed_seconds": time.monotonic() - started,
        "assertions": ["original profile/paths/inodes", "nested Unicode and zero-length files", "original virtual pad accounting without physical pad files",
            "same info and unknown plugin data", "global settings unchanged by native", "original Java verifies all completed files/pieces"],
        "limitations": ["Generated default-priority linear local workload only", "Other storage/policy/platform/charset and full-client gates remain open"]})
    print("PASS original Java -> native multifile -> original Java; type", args.type, flush=True)


if __name__ == "__main__":
    main()
