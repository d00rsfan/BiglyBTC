#!/usr/bin/env python3
"""Exercise the bundle audit without using a host Mach-O toolchain."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import call, patch

import mac_bundle
import smoke_macos_dmg


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

    def test_vendor_libraries_and_their_dependencies_are_relocated(self):
        vendor = self.app.parent / "vendor"
        vendor.mkdir()
        archive = vendor / "libarchive.13.dylib"
        compression = vendor / "libzstd.1.dylib"
        archive.write_bytes(b"archive")
        compression.write_bytes(b"compression")
        bundled_archive = self.frameworks / archive.name
        bundled_compression = self.frameworks / compression.name
        dependencies = {self.executable: [str(archive)], bundled_archive: [str(compression)]}
        with patch.object(mac_bundle, "linked_libraries", side_effect=lambda path: dependencies.get(path, [])), \
             patch.object(mac_bundle, "runtime_paths", return_value=[str(vendor)]), \
             patch.object(mac_bundle, "run") as command:
            mac_bundle.bundle_vendor_dylibs(self.frameworks, [self.executable], [vendor])
        self.assertEqual(bundled_archive.read_bytes(), b"archive")
        self.assertEqual(bundled_compression.read_bytes(), b"compression")
        command.assert_any_call("/usr/bin/install_name_tool", "-change", str(archive),
                                f"@rpath/{archive.name}", self.executable)
        command.assert_any_call("/usr/bin/install_name_tool", "-change", str(compression),
                                f"@rpath/{compression.name}", bundled_archive)

    def test_verified_sdk_bytes_cannot_be_changed_during_deployment(self):
        receipt = {"soname": f"@rpath/{mac_bundle.SDK_NAME}", "bytes": self.sdk.stat().st_size,
                   "sha256": mac_bundle.digest(self.sdk)}
        mac_bundle.require_sdk(self.sdk, receipt)
        self.sdk.write_bytes(b"changed SDK content!!")
        with self.assertRaisesRegex(ValueError, "SDK bytes changed"):
            mac_bundle.require_sdk(self.sdk, receipt)

    def test_qt_deployment_keeps_the_preverified_sdk(self):
        source = self.app.parent / mac_bundle.SDK_NAME
        source.write_bytes(self.sdk.read_bytes())
        receipt = {"soname": f"@rpath/{mac_bundle.SDK_NAME}", "bytes": source.stat().st_size,
                   "sha256": mac_bundle.digest(source)}

        def deploy(*command):
            self.assertEqual(command, ("macdeployqt", self.app, "-no-strip", "-no-codesign", f"-libpath={source.parent}"))
            mac_bundle.require_sdk(self.sdk, receipt)
            self.sdk.write_bytes(b"relocated by Qt deployment")
            return ""

        with patch.object(mac_bundle, "run", side_effect=deploy):
            self.assertEqual(mac_bundle.deploy_qt(self.app, source, "macdeployqt"), self.sdk)
        mac_bundle.require_sdk(self.sdk, receipt)
        mac_bundle.require_sdk(source, receipt)

    def test_bundle_resources_contain_database_attribution_and_runtime_provenance(self):
        assets = self.app.parent / "assets"
        assets.mkdir()
        receipt = assets / "sdk-receipt.json"
        license_file = assets / "COPYING"
        for path in (receipt, license_file, assets / "DBIP-Country.mmdb", assets / "DBIP-ASN.mmdb"):
            path.write_bytes(path.name.encode())
        resources = self.app / "Contents" / "Resources"
        resources.mkdir()
        mac_bundle.stage_resources(self.app, assets, receipt, license_file)
        self.assertEqual((resources / "geoip" / "DBIP-Country.mmdb").read_bytes(), b"DBIP-Country.mmdb")
        self.assertEqual((resources / "geoip" / "DBIP-ASN.mmdb").read_bytes(), b"DBIP-ASN.mmdb")
        self.assertIn("DB-IP", (resources / "geoip" / "ATTRIBUTION.txt").read_text())
        self.assertEqual((resources / "release-sdk.json").read_bytes(), receipt.read_bytes())
        self.assertEqual((resources / "libtorrent-copyright").read_bytes(), license_file.read_bytes())


class RelocatedSmokeTest(unittest.TestCase):
    def test_binary_is_passed_before_lipo_architecture_list(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary).resolve()
            app = work / "relocated app" / "BiglyBTC.app"
            binary = app / "Contents" / "MacOS" / "BiglyBTC"
            lipo = ("/usr/bin/lipo", binary, "-verify_arch", "arm64")
            bad_architecture = subprocess.CalledProcessError(1, lipo)
            with patch.object(smoke_macos_dmg, "run", side_effect=["", bad_architecture]) as command:
                with self.assertRaises(subprocess.CalledProcessError):
                    smoke_macos_dmg.smoke_app(app, work)
            self.assertEqual(command.call_args_list, [
                call("/usr/bin/codesign", "--verify", "--deep", "--strict", app),
                call(*lipo),
            ])
            self.assertFalse((work / "test home").exists())


if __name__ == "__main__":
    unittest.main()
