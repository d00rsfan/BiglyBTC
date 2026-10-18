#!/usr/bin/env python3
"""Generate source-built Java inputs and capture the loopback engine experiment."""

import argparse
import json
import pathlib
import subprocess

from oracle import JAVA, run, verify_classes
from sources import ROOT, save, sha256


def hashes(directory):
    return {str(path.relative_to(directory)): sha256(path.read_bytes())
        for path in sorted(directory.rglob("*")) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--classes", type=pathlib.Path, required=True)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    oracle = verify_classes(args.classes)
    args.work.mkdir(parents=True, exist_ok=False)
    harness = ROOT / "migration/fixtures/java/InteropOracle.java"
    run([JAVA / "javac", "-J-XX:ActiveProcessorCount=2", "--release", "8", "-cp", args.classes,
        "-d", args.work, harness], args.work / "javac.log", timeout=90)
    fixtures = args.work / "fixtures"
    run([JAVA / "java", "-XX:ActiveProcessorCount=2", "--add-opens=java.base/java.net=ALL-UNNAMED",
        "-Dazureus.config.path=" + str(args.work / "isolated-settings"),
        "-cp", str(args.work) + ":" + str(args.classes), "InteropOracle", "generate", fixtures],
        args.work / "generate.log", timeout=45)
    before = hashes(fixtures)
    log = args.work / "engine.log"
    timing = args.work / "resources.json"
    run(["/usr/bin/time", "-f", '{"rss_kib":%M,"elapsed_seconds":%e,"user_seconds":%U,"system_seconds":%S}',
        "-o", timing, args.probe, fixtures, args.work / "run"], log, timeout=180)
    if before != hashes(fixtures):
        raise ValueError("Engine experiment changed original Java fixtures")
    output = log.read_text()
    if "PASS all engine fixture variants" not in output:
        raise ValueError("Missing engine completion marker")
    save(args.output, {"schema_version": 1, "status": "passed",
        "source_commit": oracle["source_commit"], "core_class_digest": oracle["core_class_digest"],
        "harness_sha256": sha256(harness.read_bytes()), "probe_sha256": sha256(args.probe.read_bytes()),
        "probe_source_sha256": sha256((ROOT / "migration/probes/engine/main.cpp").read_bytes()),
        "generated_inputs": before, "resources_single_run": json.loads(timing.read_text()),
        "output": output, "network": "loopback TCP only; DHT/LSD/PEX/UPnP/NAT-PMP/uTP disabled; trackers/web seeds empty",
        "limitations": ["Both transfer peers use the native engine; no Java peer-wire session yet",
            "Resume serialization is native-engine-only, not Java-compatible .dat state",
            "No compact/reordered/file-link storage adapter or full engine gap decision",
            "Single-run resources are diagnostic, not comparative performance evidence",
            "Linux feasibility experiment only; no production engine selection"]})
    print(output, end="")


if __name__ == "__main__":
    main()
