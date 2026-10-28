#!/usr/bin/env python3
"""Owned temporary Git fixtures for the explicit retained-audit resolver."""

import hashlib
import importlib.util
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("registry_validator", pathlib.Path(__file__).with_name("validate.py"))
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class LegacyNativeResolverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="biglybtc-audit-resolver-")
        self.addCleanup(self.temporary.cleanup)
        self.parent = pathlib.Path(self.temporary.name)
        self.original = self.parent / "BiglyBT"
        self.native = self.parent / "BiglyBTC"
        for repo in (self.original, self.native):
            repo.mkdir()
            self.git(repo, "init", "-q")
            self.git(repo, "config", "user.name", "Owned resolver fixture")
            self.git(repo, "config", "user.email", "fixture@invalid.example")
        self.git(self.original, "remote", "add", "origin", VALIDATOR.ORIGINAL_ORIGIN)
        self.audit = self.commit(self.original, "upstream", "original source")
        self.legacy = self.commit(self.original, "legacy.cpp", "retained implementation")
        self.anchor = self.write_anchor([self.legacy])
        self.sources = {"BiglyBT": {"url": VALIDATOR.ORIGINAL_ORIGIN, "audit_commit": self.audit}}
        self.config = self.configuration([self.legacy])
        self.save_config()
        self.patch = mock.patch.object(VALIDATOR, "ROOT", self.native)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def git(self, repo, *args, data=None):
        return subprocess.check_output(["git", "--no-replace-objects", "-C", str(repo), *args],
            input=data, stderr=subprocess.PIPE,
            env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_OPTIONAL_LOCKS="0"))

    def commit(self, repo, name, value):
        file = repo / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(value)
        self.git(repo, "add", name)
        self.git(repo, "commit", "-q", "-m", "Owned fixture " + name)
        return self.git(repo, "rev-parse", "HEAD").decode().strip()

    def tree(self, commit):
        return self.git(self.original, "rev-parse", commit + "^{tree}").decode().strip()

    def write_anchor(self, commits):
        self.anchor_registry = json.dumps({"schema_version": 1, "upstreams": [{"id": "BiglyBT",
            "url": VALIDATOR.ORIGINAL_ORIGIN, "audit_commit": self.audit}],
            "components": [{"native_commits": [{"repository": ".", "commit": c} for c in commits]}],
            "update_reviews": []}, indent=2) + "\n"
        return self.commit(self.original, "migration/upstreams.lock.json", self.anchor_registry)

    def configuration(self, commits):
        return {"schema_version": 1, "repository": ".", "path": "../BiglyBT",
            "origin": VALIDATOR.ORIGINAL_ORIGIN, "upstream_audit_commit": self.audit,
            "audit_anchor": {"commit": self.anchor, "tree": self.tree(self.anchor),
                "registry_path": "migration/upstreams.lock.json",
                "registry_sha256": hashlib.sha256(self.anchor_registry.encode()).hexdigest()},
            "commits": {c: self.tree(c) for c in commits}}

    def save_config(self):
        path = self.native / "migration/legacy_native_repository.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.config))

    def resolve(self, commit=None, repository="."):
        return VALIDATOR.native_commit_repository(
            {"repository": repository, "commit": commit or self.legacy}, self.sources, {})

    def rejects(self):
        self.save_config()
        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
            self.resolve()

    def test_exact_declared_commit_resolves_without_changing_either_checkout(self):
        before = {p: self.git(p, "status", "--porcelain=v1", "--untracked-files=all")
            for p in (self.original, self.native)}
        self.assertEqual(self.resolve(), self.original)
        self.assertEqual(before, {p: self.git(p, "status", "--porcelain=v1", "--untracked-files=all")
            for p in before})

    def test_original_root_prefers_local_objects_and_needs_no_manifest(self):
        with mock.patch.object(VALIDATOR, "ROOT", self.original):
            self.assertEqual(self.resolve(), self.original)

    def test_native_root_prefers_its_local_objects_and_needs_no_manifest(self):
        local = self.commit(self.native, "native.cpp", "local implementation")
        (self.native / "migration/legacy_native_repository.json").unlink()
        self.assertEqual(self.resolve(local), self.native)

    def test_biglybtc_alias_does_not_acquire_the_dot_fallback(self):
        self.assertEqual(self.resolve(repository="BiglyBTC"), self.native)

    def test_undeclared_real_original_object_is_refused(self):
        with self.assertRaisesRegex(ValueError, "not declared"):
            self.resolve(self.audit)

    def test_wrong_checkout_origin_is_refused(self):
        self.git(self.original, "remote", "set-url", "origin", "https://example.invalid/BiglyBT.git")
        self.rejects()

    def test_wrong_declared_origin_is_refused(self):
        self.config["origin"] = "https://example.invalid/BiglyBT.git"
        self.rejects()

    def test_other_repository_paths_are_refused(self):
        self.config["path"] = "../other/../BiglyBT"
        self.rejects()

    def test_sibling_symlink_escape_is_refused(self):
        other = self.parent / "elsewhere"
        self.original.rename(other)
        self.original.symlink_to(other, target_is_directory=True)
        self.rejects()

    def test_wrong_upstream_anchor_is_refused(self):
        self.config["upstream_audit_commit"] = self.legacy
        self.rejects()

    def test_wrong_audit_tree_is_refused(self):
        self.config["audit_anchor"]["tree"] = self.tree(self.audit)
        self.rejects()

    def test_wrong_audit_registry_digest_is_refused(self):
        self.config["audit_anchor"]["registry_sha256"] = "0" * 64
        self.rejects()

    def test_nonregistry_anchor_path_is_refused(self):
        self.config["audit_anchor"]["registry_path"] = "legacy.cpp"
        self.rejects()

    def test_wrong_declared_commit_tree_is_refused(self):
        self.config["commits"][self.legacy] = self.tree(self.audit)
        self.rejects()

    def test_added_commit_not_listed_by_immutable_audit_is_refused(self):
        self.config["commits"][self.audit] = self.tree(self.audit)
        self.rejects()

    def test_recorded_commit_outside_anchor_history_is_refused(self):
        orphan = self.git(self.original, "commit-tree", self.tree(self.legacy), "-p", self.audit,
            data=b"independent history\n").decode().strip()
        self.anchor = self.write_anchor([self.legacy, orphan])
        self.config = self.configuration([self.legacy, orphan])
        self.save_config()
        with self.assertRaisesRegex(ValueError, "not part of the retained audit history"):
            self.resolve(orphan)

    def test_duplicate_resolver_field_is_refused(self):
        path = self.native / "migration/legacy_native_repository.json"
        path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(ValueError, "Duplicate resolver field"):
            self.resolve()

    def test_missing_manifest_is_not_a_general_sibling_fallback(self):
        (self.native / "migration/legacy_native_repository.json").unlink()
        with self.assertRaisesRegex(ValueError, "explicit legacy native resolver"):
            self.resolve()


if __name__ == "__main__":
    unittest.main()
