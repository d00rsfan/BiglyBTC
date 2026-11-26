#!/usr/bin/env python3
import json
from pathlib import Path
import struct
import tempfile
import unittest

from windows_bundle import MACHINE, SDK, inventory, pe_machine, sha256


def image(machine):
    data = bytearray(90)
    data[:2] = b"MZ"
    struct.pack_into("<I4sH", data, 60, 64, b"PE\0\0", machine)
    struct.pack_into("<H", data, 88, 0x20B)
    return data


class WindowsBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def fixture(self, arch):
        images = (
            "bin/BiglyBTC.exe", "bin/BiglyBTC-headless.exe", SDK, "bin/Qt6Core.dll", "bin/Qt6Gui.dll",
            "bin/Qt6Network.dll", "bin/Qt6Widgets.dll", "bin/platforms/qwindows.dll",
            "lib/biglybtc/lib/BiglyBTC-plugin-SpeedScheduler.dll", "lib/biglybtc/lib/BiglyBTC-plugin-azlocprov.dll",
        )
        data = (
            "bin/qt.conf", "share/BiglyBTC/geoip/DBIP-Country.mmdb", "share/BiglyBTC/geoip/DBIP-ASN.mmdb",
            "share/doc/biglybtc/LICENSE",
        )
        for name in (*images, *data):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(image(MACHINE[arch]) if name in images else b"test file")
        sdk = self.root / SDK
        (self.root / "share/doc/biglybtc/release-sdk.json").write_text(json.dumps({
            "soname": sdk.name, "sha256": sha256(sdk), "bytes": sdk.stat().st_size,
        }), encoding="utf-8")

    def test_package_requires_an_architecture_consistent_application(self):
        self.fixture("arm64")
        self.assertIn(SDK, inventory(self.root, "arm64"))
        self.assertEqual(pe_machine(self.root / SDK), MACHINE["arm64"])
        plugin = self.root / "bin/platforms/qwindows.dll"
        plugin.write_bytes(image(MACHINE["x64"]))
        with self.assertRaisesRegex(ValueError, "Wrong Windows architecture"):
            inventory(self.root, "arm64")

    def test_changed_libtorrent_cannot_be_released(self):
        self.fixture("x64")
        sdk = self.root / SDK
        sdk.write_bytes(sdk.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "approved release image"):
            inventory(self.root, "x64")

    def test_missing_platform_plugin_cannot_be_released(self):
        self.fixture("x64")
        (self.root / "bin/platforms/qwindows.dll").unlink()
        with self.assertRaisesRegex(ValueError, "qwindows.dll"):
            inventory(self.root, "x64")


if __name__ == "__main__":
    unittest.main()
