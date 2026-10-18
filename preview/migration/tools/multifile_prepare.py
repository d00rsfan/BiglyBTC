#!/usr/bin/env python3
"""Characterize real original multifile metadata and stopped partial profiles."""

import argparse
import json
import pathlib
import shutil
import subprocess

from handoff import decode
from oracle import JAVA, verify_classes
from peer_interop import jvm, unused_port
from sources import ROOT, save, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--type", type=int, choices=(1, 2, 3), help="Prepare only one profile; metadata generation still covers all versions")
    parser.add_argument("--full-check", action="store_true", help="Characterize original-only checking of complete files, without native execution")
    parser.add_argument("--priorities", action="store_true", help="Save real original non-default per-file priorities and positive torrent rate limits")
    args = parser.parse_args()
    work = args.work.resolve()
    if work.parent != pathlib.Path("/tmp") or not work.name.startswith("biglybtc-"):
        raise ValueError("Only a new generated /tmp/biglybtc-* root is accepted")
    oracle = verify_classes(pathlib.Path("/tmp/biglybtc-oracle/classes"))
    fence = json.loads((ROOT / "migration/evidence/P00-04/reference-fence.json").read_text())
    if fence["status"] != "passed" or any(sha256((ROOT / "migration/fixtures/java" / name).read_bytes()) != digest
            for name, digest in fence["source_sha256"].items()):
        raise ValueError("Original engine process guard is not qualified")
    work.mkdir(parents=True, exist_ok=False)
    classes = work / "classes"
    classes.mkdir()
    sources = [ROOT / "migration/fixtures/java" / name for name in
        ("ReferenceFence.java", "EngineOracle.java", "MultiFileOracle.java", "MultiEngineOracle.java")]
    subprocess.run(list(map(str, [JAVA / "javac", "-J-XX:ActiveProcessorCount=2", "--release", "8",
        "-cp", "/tmp/biglybtc-oracle/classes", "-d", classes, *sources])), check=True, timeout=90)
    classpath = str(classes) + ":/tmp/biglybtc-oracle/classes"
    generation = work / "generation"
    generation.mkdir()
    log = work / "generate.log"
    with log.open("w") as output:
        result = subprocess.run([*jvm(generation, classpath), "MultiFileOracle", str(generation / "fixtures")],
            cwd=generation, stdout=output, stderr=subprocess.STDOUT, timeout=90)
    if result.returncode or "MULTI_METADATA 2 " not in log.read_text(errors="replace"):
        raise ValueError("Original multifile generation failed; see " + str(log))
    report = {"status": "in_progress", "source_commit": oracle["source_commit"],
        "core_class_digest": oracle["core_class_digest"], "work": str(work),
        "harnesses_sha256": {p.name: sha256(p.read_bytes()) for p in sources}, "profiles": [],
        "metadata_observations": log.read_text(errors="replace").splitlines(),
        "full_check": args.full_check,
        "custom_priorities": args.priorities,
        "limitations": ["Generated original Java metadata/profile characterization only",
            "Complete-file recheck observations do not certify native continuation" if args.full_check else "Native multifile activation/reopen is a separate result"]}
    fixtures = generation / "fixtures"
    for kind in ((args.type,) if args.type is not None else (1, 3, 2)):
        java = work / ("java-" + str(kind))
        java.mkdir()
        torrent = java / "input.torrent"
        shutil.copyfile(fixtures / ("multi-" + str(kind) + ".torrent"), torrent)
        port = unused_port()
        log = java / "prepare.log"
        with log.open("w") as output:
            result = subprocess.run([*jvm(java, classpath, (port,)), "MultiEngineOracle", str(port), "0", str(torrent),
                str(fixtures / "expected/multi-fixture"), "check-full" if args.full_check else "prepare",
                *( ["priorities"] if args.priorities else [])], cwd=java, stdout=output, stderr=subprocess.STDOUT, timeout=180)
        text = log.read_text(errors="replace")
        if result.returncode or ("MULTI_FULL_CHECK " if args.full_check else "MULTI_PARTIAL_PERSISTED") not in text:
            raise ValueError("Original multifile partial preparation failed; see " + str(log))
        profile = java / "profile"
        inventory, _ = decode((profile / "downloads.config").read_bytes())
        entry = inventory[b"downloads"][0]
        identity = entry[b"torrent_hash"].hex().upper()
        state, _ = decode((profile / "active" / (identity + ".dat")).read_bytes())
        attributes = state[b"attributes"]
        resume = state[b"resume"][b"data"]
        observation = {"torrent_type": kind, "profile": str(profile), "hash": identity,
            "save_dir": entry[b"save_dir"].decode(), "save_file": entry.get(b"save_file", b"").decode(),
            "completedbytes": entry.get(b"completedbytes"), "downloaded": entry.get(b"downloaded"),
            "priorities_keys": [k.decode() for k in entry if k.startswith(b"file_priorities")],
            "storetypes": [v.decode() for v in attributes.get(b"storetypes", [])],
            "filelinks2": [v.decode() for v in attributes.get(b"filelinks2", [])],
            "filedownloaded": attributes.get(b"filedownloaded", {}).get(b"downloaded"),
            "resume_flags": list(resume[b"resume data"]), "resume_valid": resume[b"valid"],
            "engine_observations": [line for line in text.splitlines() if line.startswith("MULTI_")],
            "profile_sha256": {str(p.relative_to(profile)): sha256(p.read_bytes()) for p in profile.rglob("*") if p.is_file()},
            "payload_sha256": {str(p.relative_to(java / "payload")): sha256(p.read_bytes()) for p in (java / "payload").rglob("*") if p.is_file()}}
        report["profiles"].append(observation)
        save(args.output, report)
        print("PREPARED original multifile type", kind, "resume", observation["resume_flags"], flush=True)
    report["status"] = "characterized" if args.full_check else "prepared"
    save(args.output, report)


if __name__ == "__main__":
    main()
