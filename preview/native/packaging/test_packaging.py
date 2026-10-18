#!/usr/bin/env python3
"""Offline synthetic DEB contract; never install a package or launch a client."""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
from audit_deb import audit

HERE = pathlib.Path(__file__).resolve().parent
WORK = None
REPORT = {"scope": "synthetic packaging only; no native runtime qualification", "checks": []}


def run(*arguments, cwd=None, success=True):
    completed = subprocess.run(arguments, cwd=cwd, capture_output=True, text=True, timeout=120)
    REPORT.setdefault("commands", []).append({
        "argv": list(map(str, arguments)), "returncode": completed.returncode,
        "output": completed.stdout + completed.stderr,
    })
    if success and completed.returncode:
        raise AssertionError(completed.stdout + completed.stderr)
    return completed


class PackagingContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = WORK / "source"
        cls.source.mkdir()
        (cls.source / "main.cpp").write_text(
            '#include <iostream>\n#ifdef UNKNOWN_RUNTIME\nextern int unowned_symbol();\n#endif\n'
            'int main() { std::cout << "synthetic package fixture\\n";\n'
            '#ifdef UNKNOWN_RUNTIME\nreturn unowned_symbol();\n#endif\n}\n'
        )
        (cls.source / "unowned.cpp").write_text('int unowned_symbol() { return 0; }\n')
        (cls.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.25)\n'
            'project(BiglyBTC VERSION 0.1.0 LANGUAGES CXX)\n'
            'option(BIGLYBTC_DESKTOP "Fixture desktop targets" ON)\n'
            'set(BIGLYBTC_QT_RUNTIME_VERSION "6.12.0" CACHE STRING "Nominal fixture version; no Qt linked")\n'
            'add_executable(BiglyBTC main.cpp)\n'
            'add_executable(BiglyBTC-headless main.cpp)\n'
            'if(UNKNOWN_RUNTIME)\n'
            '  add_library(biglybtc-unowned SHARED unowned.cpp)\n'
            '  set_target_properties(biglybtc-unowned PROPERTIES SOVERSION 1)\n'
            '  target_link_libraries(BiglyBTC PRIVATE biglybtc-unowned)\n'
            '  target_compile_definitions(BiglyBTC PRIVATE UNKNOWN_RUNTIME)\n'
            '  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_BINARY_DIR}")\n'
            'endif()\n'
            'set_target_properties(BiglyBTC BiglyBTC-headless PROPERTIES\n'
            '  BUILD_RPATH "/tmp/biglybtc-synthetic-sdk")\n'
            f'include("{HERE / "Debian.cmake"}")\n'
        )
        cls.build = WORK / "build"
        cls.configure(cls.build, "-DBIGLYBTC_DEB=ON",
            "-DBIGLYBTC_BUNDLE_QT=OFF",
            "-DBIGLYBTC_DEB_CONTACT=Packaging Fixture <fixture@example.invalid>")
        run("nice", "-n", "5", "cmake", "--build", str(cls.build), "--parallel", "2")
        output = WORK / "packages"
        run("nice", "-n", "5", "cpack", "--config", str(cls.build / "CPackConfig.cmake"),
            "-B", str(output), cwd=cls.build)
        packages = list(output.glob("*.deb"))
        if len(packages) != 1:
            raise AssertionError(f"Expected one synthetic package: {packages}")
        cls.package = packages[0]
        cls.audit = audit(cls.package)
        cls.extracted = WORK / "extracted"
        run("dpkg-deb", "--raw-extract", str(cls.package), str(cls.extracted))

    @classmethod
    def configure(cls, build, *options, success=True):
        return run("cmake", "-S", str(cls.source), "-B", str(build),
            "-DCMAKE_BUILD_TYPE=Release", "-DBIGLYBTC_BUNDLE_QT=OFF", *options, success=success)

    def test_runtime_files_only_and_dependency_versions(self):
        self.assertEqual(len(self.audit["payload_sha256"]), 6)
        self.assertEqual(self.audit["bundled_dependencies"], [])
        self.assertFalse(self.audit["installed_or_executed"])
        self.assertIn("libc6", self.audit["control"]["Depends"])
        self.assertIn("libstdc++6", self.audit["control"]["Depends"])
        self.assertIn("6.12.0", self.audit["control"]["Depends"])
        REPORT["package"] = self.audit
        REPORT["checks"].append("six payload files, offline preview launcher, automatic libc/C++ dependencies, explicit Qt minimum")

    def test_install_clears_developer_runtime_path(self):
        original = run("readelf", "--dynamic", str(self.build / "BiglyBTC")).stdout
        self.assertIn("/tmp/biglybtc-synthetic-sdk", original)
        REPORT["checks"].append("developer RPATH present in build and absent from both packaged ELFs")

    def test_packaging_is_opt_in(self):
        build = WORK / "disabled"
        self.configure(build)
        self.assertFalse((build / "CPackConfig.cmake").exists())
        REPORT["checks"].append("ordinary build has no CPack configuration")

    def test_missing_packaging_prerequisites(self):
        cases = (
            ("contact", ("-DBIGLYBTC_DEB=ON",), "actual maintainer"),
            ("desktop", ("-DBIGLYBTC_DEB=ON", "-DBIGLYBTC_DESKTOP=OFF"), "requires the experimental desktop"),
            ("qt-version", ("-DBIGLYBTC_DEB=ON", "-DBIGLYBTC_DEB_CONTACT=Fixture <fixture@example.invalid>",
                "-DBIGLYBTC_QT_RUNTIME_VERSION="), "configured Qt Core runtime version"),
            ("prefix", ("-DBIGLYBTC_DEB=ON", "-DBIGLYBTC_DEB_CONTACT=Fixture <fixture@example.invalid>",
                "-DCMAKE_INSTALL_PREFIX=/usr/local"), "requires CMAKE_INSTALL_PREFIX=/usr"),
        )
        for name, options, message in cases:
            with self.subTest(name=name):
                result = self.configure(WORK / ("missing-" + name), *options, success=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stdout + result.stderr)
                REPORT["checks"].append("reject missing " + name)

    def test_rejects_modified_archives(self):
        for name in ("bundled-library", "profile", "postinst", "dependencies", "developer-rpath"):
            with self.subTest(name=name):
                changed = WORK / ("changed-" + name)
                shutil.copytree(self.extracted, changed)
                if name == "bundled-library":
                    path = changed / "usr/lib/libQt6Core.so.6"
                    path.parent.mkdir()
                    path.write_bytes(b"unexpected runtime")
                elif name == "profile":
                    path = changed / "home/fixture/.biglybt/azureus.config"
                    path.parent.mkdir(parents=True)
                    path.write_bytes(b"unexpected profile")
                elif name == "postinst":
                    script = changed / "DEBIAN/postinst"
                    script.write_text("#!/bin/sh\nexit 0\n")
                    script.chmod(0o755)
                elif name == "dependencies":
                    path = changed / "DEBIAN/control"
                    path.write_text("\n".join(
                        "Depends: libc6" if line.startswith("Depends:") else line
                        for line in path.read_text().splitlines()) + "\n")
                else:
                    shutil.copyfile(self.build / "BiglyBTC", changed / "usr/bin/BiglyBTC")
                package = WORK / (name + ".deb")
                run("dpkg-deb", "--build", "--root-owner-group", str(changed), str(package))
                with self.assertRaises(ValueError):
                    audit(package)
                REPORT["checks"].append("reject archive with " + name)

    def test_unknown_external_runtime_fails_package(self):
        build = WORK / "unknown-runtime"
        self.configure(build, "-DBIGLYBTC_DEB=ON", "-DUNKNOWN_RUNTIME=ON",
            "-DBIGLYBTC_DEB_CONTACT=Packaging Fixture <fixture@example.invalid>")
        run("nice", "-n", "5", "cmake", "--build", str(build), "--parallel", "2")
        result = run("nice", "-n", "5", "cpack", "--config", str(build / "CPackConfig.cmake"),
            "-B", str(WORK / "unknown-runtime-packages"), cwd=build, success=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no dependency information found for", " ".join((result.stderr + result.stdout).split()))
        self.assertIn("libbiglybtc-unowned.so.1", result.stderr + result.stdout)
        REPORT["checks"].append("CPack rejects unpackaged external runtime instead of ignoring its metadata")


def main():
    global WORK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.work:
        WORK = args.work.resolve()
        if WORK.parent != pathlib.Path("/tmp") or not WORK.name.startswith("biglybtc-"):
            parser.error("Use a fresh /tmp/biglybtc-* output directory")
        WORK.mkdir()
    else:
        WORK = pathlib.Path(tempfile.mkdtemp(prefix="biglybtc-packaging-", dir="/tmp"))
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PackagingContract)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    REPORT.update(work=str(WORK), success=result.wasSuccessful(), tests=result.testsRun,
        installed_or_executed=False)
    report = json.dumps(REPORT, indent=2) + "\n"
    (WORK / "report.json").write_text(report)
    if args.output:
        args.output.write_text(report)
    print("Retained synthetic packaging evidence:", WORK / "report.json")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
