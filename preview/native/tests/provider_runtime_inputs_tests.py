#!/usr/bin/env python3
"""Exact provider input/packaging gates. No LT loader/owner/network invocation.

Root executes after composing the overlay with the existing native tree. All
mutations are owned temporary copies, never the retained provider/source SDK.
Actual exported-marker rejection remains the separate compiled wire/ABI lane.
"""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
NATIVE = Path(__file__).resolve().parents[1]
BINDING = NATIVE / 'engine/runtime-binding'
MANIFEST_SHA = 'e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737'
RECORD_SHA = '34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f'
ARTIFACT_SHA = 'd367afd6499859c5a57d66bddd352a977ff76c7fb76b5925302c2c9008c7ddbd'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ProviderRuntimeInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.verifier = load('provider_input_verifier', BINDING / 'verify_runtime_manifest.py')
        cls.packaging = load('provider_input_packaging', NATIVE.parent / 'migration/tools/stage_preview.py')
        cls.manifest_path = BINDING / 'provider-runtime.json'
        cls.manifest = json.loads(cls.manifest_path.read_bytes())
        cls.artifact = Path(cls.manifest['artifact'])
        cls.include = Path(cls.manifest['install_root']) / 'include'

    def cloned_binding(self, root):
        for name in ('provider-runtime.json', 'provider-source-build-record.json', 'source-build-record.json',
                     'provider-dynamic-symbols.txt', 'provider-dynamic-section.txt'):
            (root / name).write_bytes((BINDING / name).read_bytes())
        return root / 'provider-runtime.json'

    def refused_cli(self, manifest, root, library=None, headers=None):
        header = root / 'refused.hpp'
        receipt = root / 'refused.json'
        command = [sys.executable, str(BINDING / 'verify_runtime_manifest.py'), '--manifest', str(manifest),
                   '--library', str(library or self.artifact), '--include', str(headers or self.include),
                   '--header', str(header), '--receipt', str(receipt)]
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
        result = subprocess.run(command, env=environment, capture_output=True, timeout=30)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(header.exists(), 'rejected input must not grant a compiled binding')
        self.assertFalse(receipt.exists(), 'rejected input must not produce a passing receipt')
        return result.stderr

    def test_exact_approved_source_and_sdk_produce_only_qualification_binding(self):
        self.assertEqual(digest(self.manifest_path), MANIFEST_SHA)
        self.assertEqual(digest(BINDING / 'provider-source-build-record.json'), RECORD_SHA)
        self.assertEqual(digest(self.artifact), ARTIFACT_SHA)
        result, pin = self.verifier.inspect(self.manifest_path, self.artifact, self.include)
        self.assertEqual(pin, MANIFEST_SHA)
        self.assertIs(result['causal_qualified'], False)
        self.assertEqual(result['purpose'], 'source-bound-qualification-only')
        self.assertIn('aa5f1767640fc1ca5f978360ddb8eebd26fc29b2f0c34b44fdf7416299a1bb3c', self.verifier.APPROVED)

    def test_changed_manifest_cannot_self_approve_or_emit_outputs(self):
        with tempfile.TemporaryDirectory(prefix='biglybtc-provider-input-') as directory:
            root = Path(directory)
            manifest = self.cloned_binding(root)
            changed = dict(self.manifest, causal_qualified=True)
            manifest.write_text(json.dumps(changed))
            self.assertIn(b'reviewed catalog', self.refused_cli(manifest, root))

    def test_changed_or_missing_record_never_grants_authority(self):
        with tempfile.TemporaryDirectory(prefix='biglybtc-provider-input-') as directory:
            root = Path(directory)
            manifest = self.cloned_binding(root)
            (root / 'provider-source-build-record.json').write_bytes(b'{}\n')
            self.assertIn(b'Changed provider or original parent record', self.refused_cli(manifest, root))
            (root / 'provider-source-build-record.json').unlink()
            self.refused_cli(manifest, root)

    def test_altered_imported_header_copy_is_not_an_approved_sdk(self):
        with tempfile.TemporaryDirectory(prefix='biglybtc-provider-input-') as directory:
            root = Path(directory)
            altered = root / 'include/libtorrent'
            altered.mkdir(parents=True)
            name = 'libtorrent/biglybtc_payload_counters.hpp'
            (root / 'include' / name).write_bytes((self.include / name).read_bytes() + b'\n// altered\n')
            # Exercise the same real byte-map primitive that the closed284
            # installed map uses, then reject the different actual SDK root.
            with self.assertRaisesRegex(ValueError, 'Changed installed_sha256 leaf'):
                self.verifier.verify_map({'installed_sha256': {name: digest(self.include / name)}},
                                         'installed_sha256', root / 'include', 1)
            self.assertIn(b'Imported LT library/SDK differs', self.refused_cli(self.manifest_path, root, headers=root / 'include'))

    def test_changed_artifact_copy_is_not_an_approved_target(self):
        with tempfile.TemporaryDirectory(prefix='biglybtc-provider-input-') as directory:
            root = Path(directory)
            image = root / 'libtorrent-rasterbar.so.2.0.12'
            raw = bytearray(self.artifact.read_bytes())
            raw[-1] ^= 1
            image.write_bytes(raw)
            self.assertNotEqual(digest(image), ARTIFACT_SHA)
            self.assertIn(b'Imported LT library/SDK differs', self.refused_cli(self.manifest_path, root, library=image))
            # Old explicit portable lane still accepts only c35, regardless
            # of version strings or marker bytes in another artifact.
            with self.assertRaises(ValueError):
                self.packaging.source_libtorrent(image)

    def test_portable_preflight_rejects_old_build_or_wrong_record_without_outputs(self):
        with tempfile.TemporaryDirectory(prefix='biglybtc-provider-input-') as directory:
            root = Path(directory)
            generated = root / 'engine/runtime-binding'
            generated.mkdir(parents=True)
            (generated / 'configure-receipt.json').write_text(json.dumps({'artifact_sha256': 'c35f'}))
            (generated / 'persistent_runtime_config.hpp').write_text('#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 0\n')
            before = sorted(str(path.relative_to(root)) for path in root.rglob('*'))
            with self.assertRaisesRegex(ValueError, 'Application was not configured'):
                self.packaging.selected_source_libtorrent(self.artifact, self.manifest_path, root)
            self.assertEqual(before, sorted(str(path.relative_to(root)) for path in root.rglob('*')))
            receipt = {'manifest_sha256': MANIFEST_SHA, 'artifact_sha256': ARTIFACT_SHA,
                'artifact': str(self.artifact), 'source_files': 449, 'full_source_files': 1072,
                'build_rules': 13, 'bundled_inputs': 16, 'installed_files': 284, 'build_files': 4,
                'causal_qualified': False, 'purpose': 'source-bound-qualification-only',
                'provider_counter_and_swarm_candidate': True}
            (generated / 'configure-receipt.json').write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, 'generated binding does not select'):
                self.packaging.selected_source_libtorrent(self.artifact, self.manifest_path, root)
            clone = root / 'clone'
            clone.mkdir()
            manifest = self.cloned_binding(clone)
            (clone / 'provider-source-build-record.json').write_bytes(b'{}\n')
            with self.assertRaisesRegex(ValueError, 'Changed/unapproved'):
                self.packaging.selected_source_libtorrent(self.artifact, manifest, root)


if __name__ == '__main__':
    unittest.main()
