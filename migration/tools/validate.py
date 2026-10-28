#!/usr/bin/env python3
"""Check structural and semantic migration provenance; no network required."""

import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

import jsonschema

ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique(items, kind):
    ids = [item["id"] for item in items]
    require(len(ids) == len(set(ids)), f"Duplicate {kind} IDs")
    return {item["id"]: item for item in items}


def exists(path):
    candidate = (ROOT / path).resolve()
    require(candidate.is_relative_to(ROOT), f"Evidence outside repository: {path}")
    require(candidate.is_file(), f"Missing evidence: {path}")


def native_repository(repository):
    if repository in {".", "BiglyBTC"}:
        return ROOT
    # Remote source exports have separate object databases. Resolve only explicit
    # retained clones; validation never fetches, switches a checkout or uses auth.
    manifest = ROOT / "migration/repository_checkouts.json"
    require(manifest.is_file(), "Native repository requires an explicit local resolver")
    configured = json.loads(manifest.read_text())
    require(configured.get("schema_version") == 1, "Unknown repository resolver schema")
    matches = [item for item in configured["repositories"] if item["repository"] == repository]
    require(len(matches) == 1, "Unknown or ambiguous native repository")
    path = pathlib.Path(matches[0]["path"]).resolve()
    name = repository.rsplit("/", 1)[-1].removesuffix(".git")
    durable_sibling = name.startswith("BiglyBTC") and path == ROOT.parent / name
    retained_temporary = str(path).startswith("/tmp/biglybtc-")
    require((durable_sibling or retained_temporary) and (path / ".git").is_dir(),
        "Native export resolver must name its durable sibling or a retained isolated clone")
    origin = subprocess.check_output(["git", "-C", str(path), "remote", "get-url", "origin"], text=True).strip()
    ssh_origin = None
    if repository.startswith("https://github.com/"):
        ssh_origin = "git@github.com:" + repository.removeprefix("https://github.com/")
    require(origin == repository or origin == ssh_origin, "Native export clone origin mismatch")
    return path


ORIGINAL_ORIGIN = "https://github.com/BiglySoftware/BiglyBT.git"
FULL_SHA1 = re.compile(r"[0-9a-f]{40}\Z")
READ_GIT_ENV = dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_OPTIONAL_LOCKS="0")


def read_json(data):
    def unique_fields(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate resolver field: {key}")
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique_fields)


def git_read(repo, *arguments):
    # A resolver only reads local objects. Do not honour replacement refs.
    return subprocess.check_output(
        ["git", "--no-replace-objects", "-C", str(repo), *arguments],
        stderr=subprocess.PIPE, env=READ_GIT_ENV)


def commit_exists(repo, commit):
    require(isinstance(commit, str) and FULL_SHA1.fullmatch(commit), "Expected a full native commit")
    return subprocess.run(
        ["git", "--no-replace-objects", "-C", str(repo), "cat-file", "-e", commit + "^{commit}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=READ_GIT_ENV).returncode == 0


def legacy_native_repository(commit, upstreams, cache):
    """Resolve only declared pre-export objects from the retained original audit."""
    require(isinstance(commit, str) and FULL_SHA1.fullmatch(commit), "Expected a full legacy commit")
    if not cache:
        manifest = ROOT / "migration/legacy_native_repository.json"
        require(manifest.is_file() and manifest.stat().st_size <= 65536,
            "Missing or oversized explicit legacy native resolver")
        configured = read_json(manifest.read_bytes())
        require(isinstance(configured, dict) and set(configured) == {"schema_version", "repository", "path", "origin",
            "upstream_audit_commit", "audit_anchor", "commits"}, "Unknown legacy resolver fields")
        require(type(configured["schema_version"]) is int and configured["schema_version"] == 1
            and configured["repository"] == ".", "Unknown legacy resolver schema or repository")
        require(configured["path"] == "../BiglyBT", "Legacy resolver requires the canonical sibling")
        repo = (ROOT / configured["path"]).resolve()
        require(repo == ROOT.parent / "BiglyBT" and (repo / ".git").is_dir(),
            "Legacy resolver is not the canonical retained BiglyBT checkout")
        source = upstreams.get("BiglyBT", {})
        require(configured["origin"] == ORIGINAL_ORIGIN and source.get("url") == ORIGINAL_ORIGIN,
            "Legacy resolver must name the original BiglyBT origin")
        require(git_read(repo, "remote", "get-url", "origin").decode().strip() == ORIGINAL_ORIGIN,
            "Legacy audit checkout origin mismatch")
        audit = configured["upstream_audit_commit"]
        require(isinstance(audit, str) and FULL_SHA1.fullmatch(audit)
            and audit == source.get("audit_commit") and commit_exists(repo, audit),
            "Legacy upstream audit anchor mismatch")
        anchor = configured["audit_anchor"]
        require(isinstance(anchor, dict) and set(anchor) ==
            {"commit", "tree", "registry_path", "registry_sha256"}, "Unknown legacy anchor fields")
        for key in ("commit", "tree"):
            require(isinstance(anchor[key], str) and FULL_SHA1.fullmatch(anchor[key]),
                "Expected a full legacy anchor object")
        require(commit_exists(repo, anchor["commit"]), "Legacy audit anchor is not a commit")
        require(anchor["registry_path"] == "migration/upstreams.lock.json"
            and isinstance(anchor["registry_sha256"], str)
            and re.fullmatch(r"[0-9a-f]{64}", anchor["registry_sha256"]), "Invalid legacy anchor registry")
        require(git_read(repo, "rev-parse", anchor["commit"] + "^{tree}").decode().strip()
            == anchor["tree"], "Legacy audit tree mismatch")
        object_name = anchor["commit"] + ":" + anchor["registry_path"]
        require(int(git_read(repo, "cat-file", "-s", object_name)) <= 4194304,
            "Legacy audit registry exceeds its bound")
        encoded = git_read(repo, "show", object_name)
        require(hashlib.sha256(encoded).hexdigest() == anchor["registry_sha256"],
            "Legacy audit registry digest mismatch")
        anchored = read_json(encoded)
        require(isinstance(anchored, dict) and anchored.get("schema_version") == 1,
            "Unknown anchored registry schema")
        sources = [item for item in anchored["upstreams"] if item["id"] == "BiglyBT"]
        require(len(sources) == 1 and sources[0]["url"] == ORIGINAL_ORIGIN
            and sources[0]["audit_commit"] == audit, "Legacy audit source mismatch")
        recorded = {native["commit"] for section in ("components", "update_reviews")
            for item in anchored[section] for native in item["native_commits"]
            if native["repository"] == "."}
        declared = configured["commits"]
        require(isinstance(declared, dict) and 0 < len(declared) <= 1024
            and set(declared) == recorded, "Legacy commit map differs from its immutable audit")
        require(all(FULL_SHA1.fullmatch(key) and isinstance(tree, str) and FULL_SHA1.fullmatch(tree)
            for key, tree in declared.items()), "Invalid legacy commit/tree map")
        cache.update(repository=repo, anchor=anchor["commit"], commits=declared)
    require(commit in cache["commits"], "Native commit is not declared by the legacy resolver")
    repo = cache["repository"]
    require(git_read(repo, "rev-parse", commit + "^{tree}").decode().strip()
        == cache["commits"][commit], "Legacy native commit tree mismatch")
    require(subprocess.run(["git", "--no-replace-objects", "-C", str(repo), "merge-base",
        "--is-ancestor", commit, cache["anchor"]], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, env=READ_GIT_ENV).returncode == 0, "Native commit is not part of the retained audit history")
    return repo


def native_commit_repository(native, upstreams, legacy_cache):
    repo = native_repository(native["repository"])
    if native["repository"] == "." and not commit_exists(repo, native["commit"]):
        return legacy_native_repository(native["commit"], upstreams, legacy_cache)
    return repo


def validate(lock):
    schema = json.loads((ROOT / "migration/upstreams.schema.json").read_text())
    jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker()).validate(lock)
    upstreams = unique(lock["upstreams"], "upstream")
    components = unique(lock["components"], "component")
    unique(lock["external_dependencies"], "dependency")
    unique(lock["unresolved_inputs"], "unresolved input")
    reviews = unique(lock["update_reviews"], "review")
    feature_ids = set(re.findall(r"\| ([FX]\d{2}) \|", (ROOT / "migration/FEATURE_MATRIX.md").read_text()))
    for source in upstreams.values():
        repo = (ROOT / source["checkout_hint"]).resolve()
        subprocess.run(["git", "-C", str(repo), "cat-file", "-e", source["audit_commit"] + "^{commit}"], check=True)
    for component in components.values():
        for feature in component["feature_ids"]:
            require(feature.split(".")[0] in feature_ids, f"Unknown feature: {feature}")
        for path in component["task_records"] + component["review_records"] + component["evidence"]:
            exists(path)
        if component["decision_ref"]:
            exists(component["decision_ref"])
        for coverage in component["source_coverage"]:
            require(coverage["upstream_id"] in upstreams, "Unknown source coverage upstream")
            source = upstreams[coverage["upstream_id"]]
            repo = (ROOT / source["checkout_hint"]).resolve()
            for key in ("baseline_commit", "first_ported_commit", "integrated_through_commit", "last_reviewed_commit"):
                if coverage[key]:
                    subprocess.run(["git", "-C", str(repo), "cat-file", "-e", coverage[key] + "^{commit}"], check=True)
            for path in coverage["source_paths"]:
                subprocess.run(["git", "-C", str(repo), "cat-file", "-e", coverage["baseline_commit"] + ":" + path], check=True)
    for review in reviews.values():
        require(review["component_id"] in components, "Unknown review component")
        require(review["upstream_id"] in upstreams, "Unknown review upstream")
        exists(review["record"])
    legacy_cache = {}
    for record in list(components.values()) + list(reviews.values()):
        for native in record["native_commits"]:
            repo = native_commit_repository(native, upstreams, legacy_cache)
            subprocess.run(["git", "-C", str(repo), "cat-file", "-e", native["commit"] + "^{commit}"], check=True)
    print(f"PASS registry: {len(upstreams)} sources, {len(components)} components, {len(reviews)} reviews")


if __name__ == "__main__":
    try:
        validate(json.loads((ROOT / "migration/upstreams.lock.json").read_text()))
    except (ValueError, jsonschema.ValidationError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
