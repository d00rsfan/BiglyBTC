#!/usr/bin/env python3
"""Inventory immutable source inputs without changing reference repositories."""

import argparse
import hashlib
import io
import json
import pathlib
import subprocess
import tarfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCK = ROOT / "migration/upstreams.lock.json"
ARTIFACT_SUFFIXES = {".jar", ".zip", ".exe", ".dll", ".so", ".dylib", ".jnilib"}


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n")


def committed_files(repo, commit):
    return git(repo, "ls-tree", "-r", "--name-only", "-z", commit).decode().split("\0")[:-1]


def extract(archive, destination):
    """Extract regular files/directories only; source repos cannot escape the root."""
    destination.mkdir(parents=True, exist_ok=False)
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source:
            relative = pathlib.PurePosixPath(member.name)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"Unsafe archive path: {member.name}")
            target = destination.joinpath(*relative.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
                target.chmod(member.mode & 0o777)
            else:
                raise ValueError(f"Unsupported archive member: {member.name}")


def audit(retain, output):
    lock = json.loads(LOCK.read_text())
    report = {"schema_version": 1, "project": "BiglyBTC", "sources": []}
    for upstream in lock["upstreams"]:
        repo = (ROOT / upstream["checkout_hint"]).resolve()
        commit = upstream["audit_commit"]
        resolved = git(repo, "rev-parse", "--verify", commit + "^{commit}").decode().strip()
        if resolved != commit:
            raise ValueError(f"Commit mismatch for {upstream['id']}")
        origin = git(repo, "remote", "get-url", "origin").decode().strip()
        if origin.removesuffix(".git").lower() != upstream["url"].removesuffix(".git").lower():
            raise ValueError(f"Origin mismatch for {upstream['id']}: {origin}")
        files = committed_files(repo, commit)
        archive = git(repo, "archive", "--format=tar", commit)
        record = {
            "id": upstream["id"], "url": upstream["url"], "commit": commit,
            "tree": git(repo, "rev-parse", commit + "^{tree}").decode().strip(),
            "archive_sha256": sha256(archive), "file_count": len(files),
            "submodules": [], "lfs_declarations": [], "build_recipes": [],
            "notices": [], "artifacts": [], "source_census": {},
        }
        if retain:
            retained = retain / upstream["id"] / commit
            retained.mkdir(parents=True, exist_ok=True)
            archive_path = retained / "source.tar"
            if archive_path.exists() and sha256(archive_path.read_bytes()) != sha256(archive):
                raise ValueError(f"Retained archive differs: {archive_path}")
            archive_path.write_bytes(archive)
            tree = retained / "tree"
            if not tree.exists():
                extract(archive, tree)
            # Extraction is a writable build input, not a second immutable archive.
            record["retained_archive"] = str(archive_path)
        for path in files:
            p = pathlib.PurePosixPath(path)
            suffix = p.suffix.lower()
            record["source_census"][suffix or "(none)"] = record["source_census"].get(suffix or "(none)", 0) + 1
            if p.name in {"pom.xml", "build.xml", "build.gradle", "CMakeLists.txt"} or suffix == ".jardesc":
                record["build_recipes"].append(path)
            if any(word in p.name.lower() for word in ("license", "copyright", "notice")):
                record["notices"].append(path)
            if p.name == ".gitmodules":
                record["submodules"].append(git(repo, "show", f"{commit}:{path}").decode())
            if p.name == ".gitattributes":
                text = git(repo, "show", f"{commit}:{path}").decode()
                record["lfs_declarations"].extend(line for line in text.splitlines() if "filter=lfs" in line)
            if suffix not in ARTIFACT_SUFFIXES:
                continue
            data = git(repo, "show", f"{commit}:{path}")
            artifact = {"path": path, "bytes": len(data), "sha256": sha256(data)}
            if suffix in {".jar", ".zip"}:
                try:
                    with zipfile.ZipFile(io.BytesIO(data)) as bundle:
                        artifact["embedded_source"] = [name for name in bundle.namelist()
                            if pathlib.PurePosixPath(name).suffix.lower() in {".java", ".c", ".cpp", ".h"}]
                        if "META-INF/MANIFEST.MF" in bundle.namelist():
                            artifact["manifest"] = bundle.read("META-INF/MANIFEST.MF").decode("utf-8", "replace")
                except zipfile.BadZipFile:
                    artifact["archive_error"] = "not a ZIP archive"
            record["artifacts"].append(artifact)
        report["sources"].append(record)
        print(f"PASS {upstream['id']}: {commit} ({len(files)} files, {len(record['artifacts'])} artifacts)", flush=True)
    save(output, report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    command = sub.add_parser("audit")
    command.add_argument("--retain", type=pathlib.Path)
    command.add_argument("--output", type=pathlib.Path,
        default=ROOT / "migration/evidence/P00-01/sources.json")
    args = parser.parse_args()
    audit(args.retain.resolve() if args.retain else None, args.output)


if __name__ == "__main__":
    main()
