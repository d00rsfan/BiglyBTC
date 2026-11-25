#!/usr/bin/env python3
"""Exercise the bundle audit without using a host Mach-O toolchain."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import mac_bundle


class BundleAudit(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.app = Path(temporary.name).resolve() / "BiglyBTC.app"
        self.frameworks = self.app / "Contents" / "Frameworks"
        self.frameworks.mkdir(parents=True)
        self.executable = self.app / "Contents" / "MacOS" / "BiglyBTC"
        self.executable.parent.mkdir()
        self.executable.touch()
        self.sdk = self.frameworks / mac_bundle.SDK_NAME
        self.sdk.write_bytes(b"verified SDK contents")

    def audit(self, dependency, rpath=mac_bundle.BUNDLE_RUNTIME):
        def tool(*command):
            if command[1] == "-l":
                return f"Load command 0\n cmd LC_RPATH\n cmdsize 40\n path {rpath} (offset 12)"
            return f"BiglyBTC:\n\t{dependency} (compatibility version 2.0.0, current version 2.0.12)"

        with patch.object(mac_bundle, "run", side_effect=tool):
            mac_bundle.audit_dependencies(self.executable, self.app)

    def test_relative_framework_and_system_library(self):
        self.audit(f"@rpath/{mac_bundle.SDK_NAME}")
        self.audit("/usr/lib/libSystem.B.dylib")

    def test_external_and_missing_dependencies_rejected(self):
        for library in ("/opt/local/lib/libarchive.dylib", "@rpath/missing.dylib", "@loader_path/../../../foreign.dylib"):
            with self.subTest(library=library), self.assertRaisesRegex(ValueError, "external or missing library"):
                self.audit(library)

    def test_external_runtime_search_path_rejected(self):
        with self.assertRaisesRegex(ValueError, "runtime search path outside the bundle"):
            self.audit(f"@rpath/{mac_bundle.SDK_NAME}", "/opt/homebrew/lib")

    def test_verified_sdk_bytes_cannot_be_changed_during_deployment(self):
        receipt = {"soname": f"@rpath/{mac_bundle.SDK_NAME}", "bytes": self.sdk.stat().st_size,
                   "sha256": mac_bundle.digest(self.sdk)}
        mac_bundle.require_sdk(self.sdk, receipt)
        self.sdk.write_bytes(b"changed SDK content!!")
        with self.assertRaisesRegex(ValueError, "SDK bytes changed"):
            mac_bundle.require_sdk(self.sdk, receipt)


if __name__ == "__main__":
    unittest.main()
