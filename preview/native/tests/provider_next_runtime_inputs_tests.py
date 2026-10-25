"""Root runs only after exact review/catalog amendment, never against the draft."""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
BINDING = Path(__file__).resolve().parents[1] / "engine/runtime-binding"
MANIFEST_SHA = "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"
RECORD_SHA = "3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca"


class ProviderNextInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not re.fullmatch(r"[0-9a-f]{64}", MANIFEST_SHA):
            raise ValueError("UNAPPROVED next-provider draft is not a passing qualification lane")
        spec = importlib.util.spec_from_file_location("next_provider_outer_verifier", BINDING / "verify_runtime_manifest.py")
        cls.verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.verifier)
        cls.path = BINDING / "provider-next-runtime.json"
        cls.manifest = json.loads(cls.path.read_bytes())
        cls.artifact = Path(cls.manifest["artifact"])
        cls.include = Path(cls.manifest["install_root"]) / "include"

    def clone(self, root):
        for name in ("provider-next-runtime.json", "provider-next-source-build-record.json", "provider-source-build-record.json"):
            (root / name).write_bytes((BINDING / name).read_bytes())
        return root / "provider-next-runtime.json"

    def refused(self, path, root, library=None, headers=None):
        header, receipt = root / "refused.hpp", root / "refused.json"
        result = subprocess.run([sys.executable, "-B", str(BINDING / "verify_runtime_manifest.py"),
            "--manifest", str(path), "--library", str(library or self.artifact),
            "--include", str(headers or self.include), "--header", str(header), "--receipt", str(receipt)],
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"), capture_output=True, timeout=30)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(header.exists())
        self.assertFalse(receipt.exists())
        return result.stderr

    def test_exact_new_and_original_provider_branches_both_remain_closed(self):
        result, pin = self.verifier.inspect(self.path, self.artifact, self.include)
        self.assertEqual(pin, MANIFEST_SHA)
        self.assertFalse(result["causal_qualified"])
        record = json.loads((BINDING / "provider-next-source-build-record.json").read_bytes())
        self.assertEqual(self.verifier.digest(BINDING / "provider-next-source-build-record.json"), RECORD_SHA)
        self.assertEqual((len(record["source_inputs_sha256"]), len(record["source_sha256"]),
            len(record["installed_sha256"])), (1074, 451, 285))
        old = json.loads((BINDING / "provider-runtime.json").read_bytes())
        previous, old_pin = self.verifier.inspect(BINDING / "provider-runtime.json", Path(old["artifact"]), Path(old["install_root"]) / "include")
        self.assertEqual(old_pin, "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737")
        self.assertFalse(previous["causal_qualified"])

    def test_manifest_cannot_self_approve_or_grant_causal_credit(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-next-provider-input-") as area:
            root = Path(area)
            path = self.clone(root)
            path.write_text(json.dumps(dict(self.manifest, causal_qualified=True)))
            self.assertIn(b"reviewed catalog", self.refused(path, root))

    def test_missing_or_changed_record_is_rejected_before_generated_outputs(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-next-provider-input-") as area:
            root = Path(area)
            path = self.clone(root)
            record = root / "provider-next-source-build-record.json"
            record.write_bytes(b"{}\n")
            self.assertIn(b"Changed next-provider", self.refused(path, root))
            record.unlink()
            self.refused(path, root)

    def test_imported_sdk_and_same_bytes_unbound_image_copy_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-next-provider-input-") as area:
            root = Path(area)
            include = root / "include"
            include.mkdir()
            self.assertIn(b"Imported LT library/SDK differs", self.refused(self.path, root, headers=include))
            image = root / self.artifact.name
            image.write_bytes(self.artifact.read_bytes())
            self.assertIn(b"Imported LT library/SDK differs", self.refused(self.path, root, library=image))


if __name__ == "__main__":
    unittest.main()
