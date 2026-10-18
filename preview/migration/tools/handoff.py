#!/usr/bin/env python3
"""Continue and reopen the SAME generated Java profile, without import/conversion."""

import argparse
import json
import pathlib
import subprocess
import time

from oracle import JAVA, verify_classes
from peer_interop import jvm, unused_port, owned_peer_port
from sources import ROOT, save, sha256


def decode(data):
    """Trusted generated-fixture assertions only; production uses bounded C++."""
    position = 0
    spans = {}

    def read(path=()):
        nonlocal position
        start = position
        marker = data[position:position + 1]
        position += 1
        if marker == b"i":
            end = data.index(b"e", position)
            result = int(data[position:end])
            position = end + 1
        elif marker == b"l":
            result = []
            while data[position:position + 1] != b"e":
                result.append(read(path + (len(result),)))
            position += 1
        elif marker == b"d":
            result = {}
            while data[position:position + 1] != b"e":
                key = read()
                result[key] = read(path + (key,))
            position += 1
        else:
            position -= 1
            colon = data.index(b":", position)
            length = int(data[position:colon])
            position = colon + 1 + length
            result = data[colon + 1:position]
        spans[path] = data[start:position]
        return result

    result = read()
    if position != len(data):
        raise ValueError("Trailing generated fixture bytes")
    return result, spans


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=pathlib.Path, required=True)
    parser.add_argument("--seed", type=pathlib.Path, required=True)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--type", type=int, choices=(1, 2, 3), required=True)
    parser.add_argument("--prepared", action="store_true")
    parser.add_argument("--crash", type=int, choices=range(6))
    parser.add_argument("--recover", action="store_true", help="Recover the same files in native, then reopen Java")
    parser.add_argument("--java-first", action="store_true", help="Original Java reads the interrupted profile before native recovery")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.recover and args.crash is None:
        parser.error("--recover requires --crash")
    if args.java_first and not args.recover:
        parser.error("--java-first requires --recover")
    work = args.work.resolve()
    if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-"):
        raise ValueError("Only explicitly generated /tmp/biglybtc-* roots are accepted")
    classes = pathlib.Path("/tmp/biglybtc-oracle/classes")
    oracle = verify_classes(classes)
    preparation = work / "preparation.json"
    if not args.prepared:
        subprocess.run(["python3", str(ROOT / "migration/tools/prepare_profile.py"), "--type", str(args.type),
            "--work", str(work), "--output", str(preparation)], check=True, timeout=240)
    elif not preparation.exists():
        # The first independently retained prepared profile has its original
        # manifest in the repository. Never infer authority from an arbitrary path.
        preparation = ROOT / "migration/evidence/P00-04/handoff-v1-prepared.json"
    prepared = json.loads(preparation.read_text())
    profile = work / "java/profile"
    if prepared["status"] != "prepared" or pathlib.Path(prepared["profile"]) != profile or prepared["torrent_type"] != args.type:
        raise ValueError("Prepared profile identity mismatch")
    for name, digest in prepared["profile_files_sha256"].items():
        if sha256((profile / name).read_bytes()) != digest:
            raise ValueError("Prepared profile was modified before its first native handoff: " + name)
    fence = json.loads((ROOT / "migration/evidence/P00-04/reference-fence.json").read_text())
    if fence["status"] != "passed" or any(sha256((ROOT / "migration/fixtures/java" / name).read_bytes()) != digest
            for name, digest in fence["source_sha256"].items()):
        raise ValueError("Reference guard source is not qualified")
    compiled = work / "classes"
    sources = [ROOT / "migration/fixtures/java" / name for name in ("ReferenceFence.java", "EngineOracle.java")]
    classpath = str(compiled) + ":" + str(classes)
    subprocess.run(list(map(str, [JAVA / "javac", "-J-XX:ActiveProcessorCount=2", "--release", "8",
        "-cp", classpath, "-d", compiled, *sources])), check=True, timeout=90)
    original_config = (profile / "biglybt.config").read_bytes()
    original_inventory, _ = decode((profile / "downloads.config").read_bytes())
    entry = original_inventory[b"downloads"][0]
    native_entry_before = entry
    identity = entry[b"torrent_hash"].hex().upper()
    state_path = profile / "active" / (identity + ".dat")
    original_state, original_spans = decode(state_path.read_bytes())
    original_payload = pathlib.Path(prepared["payload"])
    original_identity = (original_payload.stat().st_dev, original_payload.stat().st_ino)
    expected = pathlib.Path(prepared["expected"]).read_bytes()
    fixtures = work / "generation/fixtures"
    started = time.monotonic()
    native_log = work / "native-handoff.log"
    with (work / "seed.log").open("w") as log:
        seed = subprocess.Popen([str(args.seed), "--seed", str(fixtures), str(args.type), str(work / "handoff-seed")],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            ready = work / "handoff-seed/ready"
            port = owned_peer_port(ready, seed)
            command = [str(args.app), "--experimental-local", str(profile), identity, str(port)]
            if args.crash is not None:
                command.extend(["--crash", str(args.crash)])
            with native_log.open("w") as output:
                result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=150)
            text = native_log.read_text(errors="replace")
            if result.returncode != (0 if args.crash is None else 66):
                raise ValueError("Native handoff failed; see " + str(native_log))
            if args.crash is None and "LOCAL_COMPLETE reused=1" not in text:
                raise ValueError("Native continuation did not retain the original verified piece")
            if args.recover:
                if (profile / "biglybt.config").read_bytes() != original_config:
                    raise ValueError("Interrupted native run changed global settings")
                interrupted_inventory, _ = decode((profile / "downloads.config").read_bytes())
                interrupted_entry = interrupted_inventory[b"downloads"][0]
                for key, value in entry.items():
                    if key not in (b"downloaded", b"completedbytes", b"allocated") and interrupted_entry.get(key) != value:
                        raise ValueError("Interrupted native run changed saved policy/counters: " + repr(key))
                if args.java_first:
                    java = work / "java"
                    listen = unused_port()
                    role = "inspect-partial" if args.crash <= 1 else "reopen"
                    command_java = jvm(java, classpath, (listen,)) + ["EngineOracle", str(listen), "0",
                        str(java / "input.torrent"), prepared["expected"], role]
                    log_java = work / "interrupted-java.log"
                    with log_java.open("w") as output:
                        result_java = subprocess.run(command_java, cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=150)
                    text_java = log_java.read_text(errors="replace")
                    wanted_java = "ENGINE_PARTIAL_RECHECK 1" if args.crash <= 1 else "ENGINE_REOPEN_PIECES 8"
                    if result_java.returncode or wanted_java not in text_java or "ENGINE_STATE_PERSISTED" not in text_java:
                        raise ValueError("Original Java could not read the interrupted native profile; see " + str(log_java))
                    original_config = (profile / "biglybt.config").read_bytes()
                    after_java_inventory, _ = decode((profile / "downloads.config").read_bytes())
                    native_entry_before = after_java_inventory[b"downloads"][0]
                recovery_log = work / "native-recovery.log"
                with recovery_log.open("w") as output:
                    result = subprocess.run(command[:-2], stdout=output, stderr=subprocess.STDOUT, timeout=150)
                recovery = recovery_log.read_text(errors="replace")
                wanted = "LOCAL_COMPLETE reused=" + ("1" if args.crash <= 1 else "8")
                if result.returncode or wanted not in recovery:
                    raise ValueError("Native crash recovery did not retain expected pieces; see " + str(recovery_log))
        finally:
            if (work / "handoff-seed").exists():
                (work / "handoff-seed/stop").touch()
            try:
                seed.wait(timeout=15)
            except subprocess.TimeoutExpired:
                seed.terminate()  # This exact Popen is our generated native seed.
                seed.wait(timeout=10)
    if (profile / "biglybt.config").read_bytes() != original_config:
        raise ValueError("Native continuation changed original global settings")
    native_state, native_spans = decode(state_path.read_bytes())
    if native_spans[(b"info",)] != original_spans[(b"info",)] or native_state[b"unknown.future.plugin"] != original_state[b"unknown.future.plugin"]:
        raise ValueError("Native continuation changed info bytes or an unknown plugin value")
    if args.crash is not None and not args.recover:
        # Early boundaries intentionally leave incomplete data. Recovery and
        # completion must be qualified separately before claiming Java reopen.
        report = {"status": "native-crash-captured", "crash_stage": args.crash,
            "native_log": str(native_log), "profile": str(profile)}
        save(args.output, report)
        print("CAPTURED native crash", args.crash)
        return
    complete = original_payload.with_name("generated.bin")
    if complete.read_bytes() != expected or original_payload.exists() or (complete.stat().st_dev, complete.stat().st_ino) != original_identity:
        raise ValueError("Completion rename/content did not match original behavior")
    native_inventory, _ = decode((profile / "downloads.config").read_bytes())
    final_entry = native_inventory[b"downloads"][0]
    for key, value in native_entry_before.items():
        if key not in (b"downloaded", b"completedbytes", b"allocated") and final_entry.get(key) != value:
            raise ValueError("Native continuation changed saved policy/counters: " + repr(key))
    if final_entry[b"completedbytes"] != len(expected) or final_entry[b"downloaded"] < native_entry_before[b"downloaded"]:
        raise ValueError("Compatible completion/counters were not saved")
    native_profile = {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()}
    java = work / "java"
    listen = unused_port()
    command = jvm(java, classpath, (listen,)) + ["EngineOracle", str(listen), "0", str(java / "input.torrent"), prepared["expected"], "reopen"]
    java_log = work / "reopen.log"
    with java_log.open("w") as output:
        result = subprocess.run(command, cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=150)
    text = java_log.read_text(errors="replace")
    if result.returncode or "ENGINE_REOPEN_PIECES 8" not in text or "ENGINE_REOPEN_VERIFIED" not in text or "ENGINE_STATE_PERSISTED" not in text:
        raise ValueError("Original Java same-profile reopen failed; see " + str(java_log))
    if complete.read_bytes() != expected:
        raise ValueError("Original Java reopen changed completed payload")
    reopened_state, reopened_spans = decode(state_path.read_bytes())
    if reopened_spans[(b"info",)] != original_spans[(b"info",)] or reopened_state[b"unknown.future.plugin"] != original_state[b"unknown.future.plugin"]:
        raise ValueError("Original Java reopen changed info bytes or unknown plugin state")
    report = {"status": "passed", "source_commit": oracle["source_commit"], "core_class_digest": oracle["core_class_digest"],
        "torrent_type": args.type, "profile": str(profile), "same_profile_at_every_stage": True,
        "steps": ["original Java partial stopped", "native retained one verified piece and continued same .az! inode",
            "native disk flush and compatible completion rename/state/inventory save", "original Java reopened eight verified pieces without peers"],
        "native_binary_sha256": sha256(args.app.read_bytes()), "seed_binary_sha256": sha256(args.seed.read_bytes()),
        "tools_sha256": {name: sha256((ROOT / "migration/tools" / name).read_bytes()) for name in
            ("handoff.py", "prepare_profile.py", "peer_interop.py", "oracle.py")},
        "native_profile_sha256": native_profile, "final_profile_sha256": {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()},
        "payload_sha256": sha256(complete.read_bytes()), "harnesses_sha256": {p.name: sha256(p.read_bytes()) for p in sources},
        "elapsed_seconds": time.monotonic() - started, "logs": [str(native_log), str(java_log)],
        "limitations": ["Generated single-file linear owned-loopback case only", "No resource comparison, broader profile/policy/storage or platform qualification"]}
    if args.recover:
        report["crash_stage"] = args.crash
        report["steps"].insert(2, "native process interrupted at selected boundary, then recovered the same files")
        report["logs"].append(str(work / "native-recovery.log"))
        if args.java_first:
            report["steps"].insert(3, "original Java read the interrupted profile before native recovery")
            report["logs"].append(str(work / "interrupted-java.log"))
    save(args.output, report)
    print("PASS same-profile Java -> BiglyBTC -> Java handoff; type", args.type)


if __name__ == "__main__":
    main()
