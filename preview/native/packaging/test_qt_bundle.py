#!/usr/bin/env python3
"""Package and relocate a tiny Qt fixture, without installing or opening a profile."""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from audit_deb import audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=pathlib.Path, default=pathlib.Path("/opt/Qt/6.12.0/gcc_64"))
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    work = pathlib.Path(tempfile.mkdtemp(prefix="biglybtc-qt-bundle-", dir="/tmp"))
    source = work / "source"
    source.mkdir()
    records = []

    def run(command, cwd=None, environment=None):
        result = subprocess.run(command, cwd=cwd, env=environment, text=True,
            capture_output=True, timeout=180)
        records.append({"argv": list(map(str, command)), "returncode": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout

    (source / "main.cpp").write_text(
        '#include <QApplication>\n#include <QLibraryInfo>\n#include <QTimer>\n#include <iostream>\n'
        'int main(int argc, char **argv) { QApplication app(argc, argv);\n'
        'std::cout << qVersion() << "\\n" << QLibraryInfo::path(QLibraryInfo::PluginsPath).toStdString() << "\\n";\n'
        'QTimer::singleShot(0, &app, &QApplication::quit); return app.exec(); }\n')
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(BiglyBTC VERSION 0.1.0 LANGUAGES CXX)\n'
        'set(CMAKE_CXX_STANDARD 23)\nset(BIGLYBTC_DESKTOP ON)\n'
        'find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Widgets)\n'
        'set(BIGLYBTC_QT_RUNTIME_VERSION "${Qt6Core_VERSION}")\n'
        f'set(BIGLYBTC_QT_RUNTIME_ROOT "{args.sdk.resolve()}")\n'
        'add_executable(BiglyBTC main.cpp)\nadd_executable(BiglyBTC-headless main.cpp)\n'
        'target_link_libraries(BiglyBTC PRIVATE Qt6::Widgets)\n'
        'target_link_libraries(BiglyBTC-headless PRIVATE Qt6::Widgets)\n'
        f'include("{here / "Debian.cmake"}")\n')
    report = {"scope": "real bundled Qt relocation with synthetic application; no profile, installation or display qualification",
        "work": str(work), "commands": records, "success": False}
    try:
        build = work / "build"
        run(["nice", "-n", "5", "cmake", "-S", str(source), "-B", str(build),
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=/usr/bin/g++-16",
            "-DBIGLYBTC_DEB=ON", "-DBIGLYBTC_BUNDLE_QT=ON",
            "-DBIGLYBTC_DEB_CONTACT=Packaging Fixture <fixture@example.invalid>",
            "-DCMAKE_PREFIX_PATH=" + str(args.sdk.resolve())])
        run(["nice", "-n", "5", "cmake", "--build", str(build), "--parallel", "2"])
        output = work / "packages"
        run(["nice", "-n", "5", "cpack", "--config", str(build / "CPackConfig.cmake"),
            "-B", str(output)], cwd=build)
        package, = output.glob("*.deb")
        report["audit"] = audit(package)
        relocated = work / "relocated"
        run(["dpkg-deb", "--extract", str(package), str(relocated)])
        executable = relocated / "usr/lib/biglybtc/bin/BiglyBTC"
        environment = os.environ.copy()
        for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"):
            environment.pop(key, None)
        for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            directory = work / key.lower()
            directory.mkdir(mode=0o700)
            environment[key] = str(directory)
        environment["QT_QPA_PLATFORM"] = "offscreen"
        linked = run(["ldd", str(executable)], environment=environment)
        if "not found" in linked or "/opt/Qt/" in linked:
            raise ValueError("Relocated executable retained an SDK dependency")
        qt_paths = re.findall(r"libQt6[^ ]+ => ([^ ]+)", linked)
        if len(qt_paths) < 3 or any(not pathlib.Path(path).resolve().is_relative_to(relocated) for path in qt_paths):
            raise ValueError("Qt runtime did not resolve inside the relocated package")
        launched = run([str(executable)], environment=environment)
        if not launched.startswith("6.12.0\n") or "/opt/Qt" in launched or str(relocated) not in launched:
            raise ValueError("Qt plugin path did not relocate with the package")
        report.update(success=True, relocated_loader=linked, fixture_output=launched,
            installed=False, live_profile_access=False)
    finally:
        text = json.dumps(report, indent=2) + "\n"
        (work / "report.json").write_text(text)
        if args.output:
            args.output.write_text(text)
        print("Retained Qt bundle evidence:", work / "report.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
