#!/usr/bin/env python3
"""Build a pristine offline Java core and capture generated format fixtures."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess

from sources import ROOT, extract, git, save, sha256


def verify_classes(classes):
    """Reject an installed/stale/modified core offered as the frozen Java oracle."""
    manifest = json.loads((ROOT / "migration/evidence/P00-03/formats.json").read_text())
    paths = sorted(path for path in classes.rglob("*.class")
        if path.relative_to(classes).as_posix() != "InteropOracle.class")
    actual = sha256(b"".join(str(path.relative_to(classes)).encode() + b"\0" + path.read_bytes()
        for path in paths))
    if actual != manifest["core_class_digest"]:
        raise ValueError("Java classes differ from the frozen source-built oracle")
    return manifest

JAVA = pathlib.Path("/usr/lib/jvm/java-17-openjdk-amd64/bin")
CASES = {
    "empty": b"de",
    "integer-zero": b"d1:ai0ee",
    "integer-negative": b"d1:ai-1ee",
    "integer-minimum": b"d1:ai-9223372036854775808ee",
    "integer-maximum": b"d1:ai9223372036854775807ee",
    "binary": b"d1:a4:\x00\xff\x80\x7fe",
    "nested": b"d1:ald1:bi42ee0:leee",
    "unordered": b"d1:zi1e1:ai2ee",
    "duplicate-key": b"d1:ai1e1:ai2ee",
    "leading-zero": b"d1:ai01ee",
    "negative-zero": b"d1:ai-0ee",
    "trailing-data": b"degarbage",
    "truncated-string": b"d1:a5:abc",
    "truncated-dictionary": b"d1:ai1e",
    "bad-integer": b"d1:ai+2ee",
    "integer-overflow": b"d1:ai9223372036854775808ee",
    "empty-integer": b"d1:aiee",
    "string-leading-zero": b"d1:a03:abce",
    "binary-key": b"d1:\xff1:xe",
}


def run(command, log, **kwargs):
    with log.open("w") as output:
        result = subprocess.run([str(arg) for arg in command], stdout=output,
            stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command exited {result.returncode}: {command[0]}; see {log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=pathlib.Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    # Existing output is never reused as a pristine build.
    work.mkdir(parents=True, exist_ok=False)
    lock = json.loads((ROOT / "migration/upstreams.lock.json").read_text())
    source = next(item for item in lock["upstreams"] if item["id"] == "BiglyBT")
    commit = source["audit_commit"]
    archive = git(ROOT, "archive", "--format=tar", commit)
    tree = work / "source"
    extract(archive, tree)
    classes = work / "classes"
    classes.mkdir()
    excluded = {"com/biglybt/core/util/spi/AENameServiceDescriptor.java"}
    paths = sorted(path for path in (tree / "core/src").rglob("*.java")
        if path.relative_to(tree / "core/src").as_posix() not in excluded)
    inputs = work / "sources.txt"
    inputs.write_text("\n".join('"' + str(path) + '"' for path in paths) + "\n")
    cli = tree / "core/lib/commons-cli.jar"
    command = [JAVA / "javac", "--release", "8", "-encoding", "ISO-8859-1",
        "-cp", cli, "-d", classes, "@" + str(inputs)]
    run(command, work / "javac.log")
    for resource in (tree / "core/src").rglob("*"):
        relative = resource.relative_to(tree / "core/src")
        if not resource.is_file() or resource.suffix in {".java", ".class"} or resource.name == ".editorconfig":
            continue
        if relative.as_posix() == "META-INF/services/sun.net.spi.nameservice.NameServiceDescriptor":
            continue
        dest = classes / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(resource, dest)
    harness = ROOT / "migration/fixtures/java/FormatOracle.java"
    run([JAVA / "javac", "--release", "8", "-cp", classes, "-d", classes, harness], work / "harness.log")
    fixture_dir = ROOT / "migration/fixtures/bencoding"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": 1, "upstream_id": "BiglyBT", "source_commit": commit,
        "source_archive_sha256": sha256(archive),
        "recipe": "migration/tools/oracle.py", "java_source_count": len(paths),
        "javac_command": [str(arg) for arg in command],
        "java_identity": subprocess.check_output([JAVA / "java", "-version"], stderr=subprocess.STDOUT).decode(),
        "commons_cli_sha256": sha256(cli.read_bytes()), "harness_sha256": sha256(harness.read_bytes()),
        "core_class_digest": sha256(b"".join(str(p.relative_to(classes)).encode() + b"\0" + p.read_bytes()
            for p in sorted(classes.rglob("*.class")))), "cases": []}
    profile = work / "isolated-profile"
    profile.mkdir()
    for name, data in CASES.items():
        path = fixture_dir / (name + ".input")
        path.write_bytes(data)
        canonical = fixture_dir / (name + ".canonical")
        # Never leave a prior ACCEPT output beside a new REJECT observation.
        if canonical.exists():
            canonical.unlink()
        result = subprocess.run([str(JAVA / "java"), "-Dazureus.config.path=" + str(profile),
            "-cp", str(classes), "FormatOracle", str(path), str(canonical)],
            capture_output=True, timeout=15, cwd=work)
        lines = result.stdout.decode("utf-8", "replace").splitlines()
        require_result = next((line for line in lines if line == "ACCEPT" or line.startswith("REJECT ")), None)
        if result.returncode or require_result is None:
            raise RuntimeError(f"Oracle failure for {name}: {result.stderr.decode(errors='replace')}")
        record = {"id": name, "input": str(path.relative_to(ROOT)), "input_sha256": sha256(data),
            "java_result": require_result}
        if canonical.exists():
            record["canonical"] = str(canonical.relative_to(ROOT))
            record["canonical_sha256"] = sha256(canonical.read_bytes())
        manifest["cases"].append(record)
        print(f"{name}: {require_result}", flush=True)
    evidence = ROOT / "migration/evidence/P00-03"
    save(evidence / "formats.json", manifest)
    shutil.copyfile(work / "javac.log", evidence / "javac.txt")
    print(f"PASS {len(CASES)} source-derived fixtures; {len(paths)} core sources; output {evidence}")


if __name__ == "__main__":
    main()
