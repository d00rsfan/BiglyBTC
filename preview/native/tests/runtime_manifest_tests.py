#!/usr/bin/env python3
"""Configure verifier tests; root executes these, author has not run them."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
DIRECTORY = Path(__file__).resolve().parents[1] / "engine" / "runtime-binding"
SPEC = importlib.util.spec_from_file_location("runtime_manifest_verifier", DIRECTORY / "verify_runtime_manifest.py")
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)
SOURCE = DIRECTORY / "approved-runtime.json"
MANIFEST = json.loads(SOURCE.read_bytes())


class RuntimeManifestTests(unittest.TestCase):
    def inspect(self, manifest=SOURCE, library=None, headers=None):
        return VERIFIER.inspect(manifest, Path(library or MANIFEST["artifact"]),
                                Path(headers or MANIFEST["install_root"]) / "include")

    def test_exact_reviewed_source_candidate_is_only_qualification_bound(self):
        result, digest = self.inspect()
        self.assertEqual(digest, "aa5f1767640fc1ca5f978360ddb8eebd26fc29b2f0c34b44fdf7416299a1bb3c")
        self.assertIs(result["causal_qualified"], False)
        self.assertEqual(result["purpose"], "source-bound-qualification-only")

    def test_caller_cannot_self_approve_arbitrary_hash_or_extra_keys(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            path = Path(directory) / "manifest.json"
            for mutation in ({"artifact_sha256": "0" * 64}, {"causal_qualified": True},
                             {"trust_me": True}, {"artifact_bytes": True}):
                altered = dict(MANIFEST); altered.update(mutation)
                path.write_text(json.dumps(altered))
                with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "reviewed catalog"):
                    self.inspect(path)

    def test_missing_record_does_not_create_any_generated_binding(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            path = Path(directory) / "approved-runtime.json"; path.write_bytes(SOURCE.read_bytes())
            with self.assertRaises(FileNotFoundError):
                self.inspect(path)
            self.assertEqual([p.name for p in Path(directory).iterdir()], [path.name])

    def test_stale_record_is_rejected_even_with_exact_approved_manifest(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            path = Path(directory) / "approved-runtime.json"; path.write_bytes(SOURCE.read_bytes())
            (path.parent / "source-build-record.json").write_bytes(b"{}\n")
            with self.assertRaisesRegex(ValueError, "Changed source-build record"):
                self.inspect(path)

    def test_stale_retention_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            path = Path(directory) / "approved-runtime.json"; path.write_bytes(SOURCE.read_bytes())
            (path.parent / "source-build-record.json").write_bytes((DIRECTORY / "source-build-record.json").read_bytes())
            (path.parent / "retained-runtime.json").write_bytes(b"{}\n")
            with self.assertRaisesRegex(ValueError, "Changed byte-retention receipt"):
                self.inspect(path)

    def test_retained_build_provenance_preserves_historical_cache_bytes(self):
        record = json.loads((DIRECTORY / "source-build-record.json").read_bytes())
        retained_cache = Path(MANIFEST["build_root"]) / "CMakeCache.txt"
        self.assertEqual(VERIFIER.digest(retained_cache), record["build_files_sha256"]["CMakeCache.txt"])
        self.assertNotEqual(MANIFEST["build_root"], record["cache"]["libtorrent_BINARY_DIR"])
        self.assertIn(record["cache"]["libtorrent_BINARY_DIR"], retained_cache.read_text())

    def test_symlinked_manifest_and_record_are_refused(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            path = Path(directory) / "approved-runtime.json"; path.symlink_to(SOURCE)
            with self.assertRaisesRegex(ValueError, "regular input"):
                self.inspect(path)
            path.unlink(); path.write_bytes(SOURCE.read_bytes())
            (path.parent / "source-build-record.json").symlink_to(DIRECTORY / "source-build-record.json")
            with self.assertRaisesRegex(ValueError, "regular input"):
                self.inspect(path)

    def test_same_version_other_target_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            other = Path(directory) / "libtorrent-rasterbar.so.2.0.12"
            # An actual copied same-byte image is still a different target path.
            other.write_bytes(Path(MANIFEST["artifact"]).read_bytes())
            with self.assertRaisesRegex(ValueError, "another artifact"):
                self.inspect(library=other)

    def test_different_imported_header_root_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            (Path(directory) / "include").mkdir()
            with self.assertRaisesRegex(ValueError, "another install"):
                self.inspect(headers=directory)

    def test_relative_escape_map_path_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            with self.assertRaisesRegex(ValueError, "Unsafe map key"):
                VERIFIER.leaf(Path(directory), "../escape")

    def test_source_proof_counts_cannot_be_silently_reduced(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-binding-config-") as directory:
            with self.assertRaisesRegex(ValueError, "Wrong source_sha256 count"):
                VERIFIER.verify_map({"source_sha256": {}}, "source_sha256", Path(directory), 444)


if __name__ == "__main__":
    unittest.main()
