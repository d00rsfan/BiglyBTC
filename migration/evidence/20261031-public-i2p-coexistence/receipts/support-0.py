#!/usr/bin/env python3
"""Save a local migration review commit without switching HEAD or user index."""

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATHS = ("native", "migration", "MIGRATION_PLAN.md", "PLUGIN_MIGRATION_AUDIT.md", "PLUGIN_SOURCES.md",
    ".github/workflows/biglybtc-linux-packaging.yml", ".github/workflows/biglybtc-linux-deb.yml")
BRANCH = "refs/heads/biglybtc/native-20261017"


def git(*arguments, environment=None):
    return subprocess.run(["git", *arguments], cwd=ROOT, env=environment, text=True,
        check=True, capture_output=True).stdout.strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def verify_snapshot_inputs(environment):
    """Reject clean filters/newline conversion changing compiled or golden bytes."""
    raw = subprocess.run(["git", "ls-files", "-s", "-z", "--", "native", "migration/fixtures"],
        cwd=ROOT, env=environment, check=True, capture_output=True).stdout
    entries = [item for item in raw.split(b"\0") if item]
    if not entries:
        return
    objects = {}
    paths = []
    for entry in entries:
        metadata, raw_name = entry.split(b"\t", 1)
        name = os.fsdecode(raw_name)
        mode, oid, stage = metadata.decode().split()
        if mode not in {"100644", "100755"} or stage != "0":
            raise ValueError("Snapshot input must be a regular resolved file: " + name)
        paths.append((name, oid))
        objects[oid] = None
    packed = subprocess.run(["git", "cat-file", "--batch"], cwd=ROOT, env=environment,
        input=("\n".join(objects) + "\n").encode(), check=True, capture_output=True).stdout
    offset = 0
    for oid in objects:
        end = packed.index(b"\n", offset)
        header = packed[offset:end].decode().split()
        if len(header) != 3 or header[0] != oid or header[1] != "blob":
            raise ValueError("Unexpected snapshot object")
        length = int(header[2])
        objects[oid] = hashlib.sha256(packed[end + 1:end + 1 + length]).digest()
        offset = end + 1 + length + 1
    for name, oid in paths:
        path = ROOT / name
        if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).digest() != objects[oid]:
            raise ValueError("Git filters changed compiled/golden bytes; set appropriate -text attributes: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-parent", required=True)
    parser.add_argument("--message-file", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if len(args.expected_parent) != 40 or any(c not in "0123456789abcdef" for c in args.expected_parent):
        raise ValueError("A full existing expected parent commit is required")
    head = git("rev-parse", "HEAD")
    index = pathlib.Path(git("rev-parse", "--path-format=absolute", "--git-path", "index"))
    before = digest(index)
    if git("rev-parse", BRANCH) != args.expected_parent:
        raise ValueError("Review branch moved; inspect before creating another checkpoint")
    git("cat-file", "-e", args.expected_parent + "^{commit}")
    message = args.message_file.read_text()
    if not message.strip():
        raise ValueError("Checkpoint needs an explicit reviewable commit message")
    with tempfile.TemporaryDirectory(prefix="biglybtc-review-index-", dir="/tmp") as temporary_name:
        temporary = pathlib.Path(temporary_name)
        environment = os.environ.copy()
        environment.update(GIT_INDEX_FILE=str(temporary / "index"), GIT_AUTHOR_NAME="Codex", GIT_AUTHOR_EMAIL="codex@localhost",
            GIT_COMMITTER_NAME="Codex", GIT_COMMITTER_EMAIL="codex@localhost")
        git("read-tree", args.expected_parent, environment=environment)
        git("add", "-A", "--", *PATHS, environment=environment)
        verify_snapshot_inputs(environment)
        changed = git("diff", "--cached", "--name-only", args.expected_parent, environment=environment).splitlines()
        if not changed or any(not any(path == prefix or path.startswith(prefix + "/") for prefix in PATHS) for path in changed):
            raise ValueError("Checkpoint is empty or contains a path outside migration scope")
        tree = git("write-tree", environment=environment)
        commit = git("-c", "commit.gpgsign=false", "commit-tree", tree, "-p", args.expected_parent,
            "-F", str(args.message_file.resolve()), environment=environment)
        # Ref advancement is the final mutation, after protecting original user state.
        if git("rev-parse", "HEAD") != head or digest(index) != before:
            raise ValueError("Original source HEAD/index changed during snapshot; review commit retained only as " + commit)
        git("update-ref", BRANCH, commit, args.expected_parent)
        if git("rev-parse", "HEAD") != head or digest(index) != before:
            raise ValueError("Original source HEAD/index changed concurrently after review-ref update")
        report = {"branch": BRANCH, "parent": args.expected_parent, "commit": commit, "tree": tree,
            "original_head_unchanged": head, "original_index_sha256_unchanged": before,
            "isolated_index": str(temporary / "index"), "changed_paths": changed,
            "scope": list(PATHS), "published": False}
    report["isolated_index_removed"] = not temporary.exists()
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("Local migration review checkpoint", commit, "original HEAD/index unchanged; no publication")


if __name__ == "__main__":
    main()
