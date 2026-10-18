#!/usr/bin/env python3
"""Run a guarded source-built Java receiver against an owned native local seed."""

import argparse
import json
import pathlib
import shutil
import socket
import subprocess
import time

from oracle import JAVA, verify_classes
from sources import ROOT, save, sha256


def jvm(work, classes, ports=()):
    for name in ("profile", "tmp", "home", "install", "prefs"):
        (work / name).mkdir(parents=True, exist_ok=True)
    return [str(JAVA / "java"), "-XX:ActiveProcessorCount=2",
        "--add-opens=java.base/java.net=ALL-UNNAMED", "-Djava.security.manager=ReferenceFence",
        "-Dbiglybtc.test.root=" + str(work), "-Dbiglybtc.test.ports=" + ",".join(map(str, ports)),
        "-Dazureus.security.manager.install=0", "-Dazureus.loadplugins=0", "-Dazureus.skipSWTcheck=1",
        "-Dazureus.config.path=" + str(work / "profile"), "-Dazureus.install.path=" + str(work / "install"),
        "-Djava.io.tmpdir=" + str(work / "tmp"), "-Duser.home=" + str(work / "home"),
        "-Djava.util.prefs.userRoot=" + str(work / "prefs"), "-cp", classes]


def unused_port():
    # Check both transports so the fixture does not select an already-used UDP
    # number while reserving its Java TCP listener. UDP is disabled in the core.
    for _ in range(100):
        with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            tcp.bind(("127.0.0.1", 0))
            port = tcp.getsockname()[1]
            try:
                udp.bind(("127.0.0.1", port))
            except OSError:
                continue
            return port
    raise RuntimeError("No unoccupied test listener endpoint")


def owned_peer_port(path, process, timeout=45):
    """Wait for the complete newline-terminated endpoint, including old writers."""
    deadline = time.monotonic() + timeout
    while True:
        if process.poll() is not None:
            raise RuntimeError("Owned peer exited before readiness: " + str(path))
        try:
            with path.open(encoding="ascii") as stream:
                value = stream.read(16)
        except FileNotFoundError:
            value = ""
        if "\n" in value:
            if not value.endswith("\n") or value.count("\n") != 1:
                raise ValueError("Malformed owned peer endpoint: " + str(path))
            digits = value[:-1]
            if not digits.isascii() or not digits.isdecimal() or not 1 <= int(digits) <= 65535:
                raise ValueError("Invalid owned peer endpoint: " + str(path))
            return int(digits)
        if len(value) >= 16:
            raise ValueError("Unbounded owned peer endpoint: " + str(path))
        if time.monotonic() >= deadline:
            raise TimeoutError("Owned peer readiness deadline: " + str(path))
        time.sleep(.02)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--classes", type=pathlib.Path, default=pathlib.Path("/tmp/biglybtc-oracle/classes"))
    parser.add_argument("--type", type=int, choices=(1, 2, 3), required=True)
    parser.add_argument("--partial", action="store_true")
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    oracle = verify_classes(args.classes)
    fence = json.loads((ROOT / "migration/evidence/P00-04/reference-fence.json").read_text())
    for name, digest in fence["source_sha256"].items():
        if sha256((ROOT / "migration/fixtures/java" / name).read_bytes()) != digest:
            raise ValueError("Reference guard source changed; repeat its bounded qualification first")
    if fence["status"] != "passed":
        raise ValueError("Reference guard has not passed")
    args.work.mkdir(parents=True, exist_ok=False)
    compiled = args.work / "classes"
    compiled.mkdir()
    names = ("ReferenceFence.java", "EngineOracle.java", "InteropOracle.java")
    sources = [ROOT / "migration/fixtures/java" / name for name in names]
    subprocess.run(list(map(str, [JAVA / "javac", "-J-XX:ActiveProcessorCount=2", "--release", "8",
        "-cp", args.classes, "-d", compiled, *sources])), check=True, timeout=90)
    classpath = str(compiled) + ":" + str(args.classes)
    generation = args.work / "generation"
    generation.mkdir()
    command = jvm(generation, classpath) + ["InteropOracle", "generate", str(generation / "fixtures")]
    with (args.work / "generate.log").open("w") as log:
        subprocess.run(command, cwd=generation, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    fixtures = generation / "fixtures"
    before = {str(p.relative_to(fixtures)): sha256(p.read_bytes()) for p in fixtures.rglob("*") if p.is_file()}
    native = args.work / "native"
    with (args.work / "native.log").open("w") as log:
        seed = subprocess.Popen([str(args.probe), "--seed", str(fixtures), str(args.type), str(native)],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            seed_port = owned_peer_port(native / "ready", seed, 30)
            java = args.work / "java"
            java.mkdir()
            torrent = java / "input.torrent"
            shutil.copyfile(fixtures / "profile" / ("generated-" + str(args.type) + ".torrent"), torrent)
            listen = unused_port()
            command = jvm(java, classpath, (seed_port, listen)) + ["EngineOracle", str(listen),
                str(seed_port), str(torrent), str(fixtures / "generated.bin"), "partial" if args.partial else "receive"]
            java_log = args.work / "java-engine.log"
            started = time.monotonic()
            with java_log.open("w") as output:
                result = subprocess.run(command, cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=150)
            elapsed = time.monotonic() - started
            text = java_log.read_text(errors="replace")
            if result.returncode or "ENGINE_STATE_PERSISTED" not in text or "ENGINE_TRANSFER_VERIFIED 131072" not in text:
                raise RuntimeError("Java/local-native engine comparison failed; see " + str(java_log))
            if args.partial and "ENGINE_PARTIAL_RECHECK 1" not in text:
                raise RuntimeError("Original incomplete suffix/piece recheck was not verified")
        finally:
            if native.exists():
                (native / "stop").write_text("stop\n")
            try:
                seed.wait(timeout=15)
            except subprocess.TimeoutExpired:
                # This Popen handle identifies only the test child we created.
                seed.terminate()
                seed.wait(timeout=10)
        if seed.returncode:
            raise RuntimeError("Native seed did not stop cleanly")
    if before != {str(p.relative_to(fixtures)): sha256(p.read_bytes()) for p in fixtures.rglob("*") if p.is_file()}:
        raise ValueError("Original generated inputs changed")
    profile = java / "profile"
    report = {"status": "passed", "source_commit": oracle["source_commit"],
        "core_class_digest": oracle["core_class_digest"], "torrent_type": args.type,
        "java_partial_file": args.partial, "generated_files_sha256": before,
        "harnesses_sha256": {p.name: sha256(p.read_bytes()) for p in sources},
        "probe_sha256": sha256(args.probe.read_bytes()), "java_elapsed_single_run": elapsed,
        "profile_files_sha256": {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()},
        "native_output": (args.work / "native.log").read_text(),
        "java_log_sha256": sha256(java_log.read_bytes()), "java_log": str(java_log),
        "limitations": ["Test-only process guard, generated data and disabled plugins/discovery",
            "One Java receiver/native seed; reverse direction and all variants still separate cases",
            "No Java/native/Java operational resume switching yet", "Not a comparative whole-client performance result"]}
    save(args.output, report)
    print("PASS original Java/native peer transfer and real compatible state persistence, type", args.type)


if __name__ == "__main__":
    main()
