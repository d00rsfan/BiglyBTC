#!/usr/bin/env python3
"""Inspect a locally built experimental DEB without installing or executing it."""

import argparse
import hashlib
import io
import json
import pathlib
import re
import subprocess
import tarfile
import tempfile


PAYLOAD = {
    "usr/bin/BiglyBTC",
    "usr/bin/BiglyBTC-headless",
    "usr/share/doc/biglybtc-experimental/copyright",
    "usr/share/doc/biglybtc-experimental/EXPERIMENTAL.txt",
    "usr/bin/BiglyBTC-preview",
    "usr/share/applications/BiglyBTC-preview.desktop",
}
MAX_ARCHIVE = 128 * 1024 * 1024


def run(*arguments):
    return subprocess.run(arguments, check=True, capture_output=True, timeout=60).stdout


def members(raw, allowed=PAYLOAD):
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        result = {}
        for member in archive:
            name = member.name
            while name.startswith("./"):
                name = name[2:]
            path = pathlib.PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError(f"Unsafe archive path: {member.name}")
            name = str(path)
            if name in result:
                raise ValueError(f"Duplicate archive member: {name}")
            if member.isdir():
                allowed_dirs = {"."} | {
                    str(parent) for file in allowed for parent in pathlib.PurePosixPath(file).parents
                }
                if name not in allowed_dirs:
                    raise ValueError(f"Unexpected directory: {name}")
                result[name] = None
            elif member.isfile() and 0 <= member.size <= MAX_ARCHIVE:
                result[name] = archive.extractfile(member).read()
            else:
                raise ValueError(f"Unexpected archive member type/size: {name}")
        return {name: data for name, data in result.items() if data is not None}


def audit(package):
    if package.stat().st_size > MAX_ARCHIVE:
        raise ValueError("Package exceeds the experimental archive bound")
    fields = {}
    for field in ("Package", "Version", "Architecture", "Depends", "Recommends"):
        fields[field] = run("dpkg-deb", "--field", str(package), field).decode().strip()
    if fields["Package"] != "biglybtc-experimental" or fields["Architecture"] != "amd64":
        raise ValueError("Unexpected package identity or architecture")
    if not re.search(r"(?:^|,\s*)python3(?:,|$)", fields["Depends"]):
        raise ValueError("Missing Python runtime for the generated-data preview launcher")
    if not re.fullmatch(r"[0-9][A-Za-z0-9.+]*~dev", fields["Version"]):
        raise ValueError("Expected an explicitly experimental development version")
    raw = run("dpkg-deb", "--fsys-tarfile", str(package))
    runtime_path = "usr/share/doc/biglybtc-experimental/qt-runtime.json"
    runtime = None
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for member in archive:
            if member.name.removeprefix("./") == runtime_path:
                if not member.isfile() or member.size > 1024 * 1024:
                    raise ValueError("Invalid private runtime manifest")
                runtime = json.loads(archive.extractfile(member).read())
                break
    allowed = PAYLOAD.copy()
    executable_paths = ("usr/bin/BiglyBTC", "usr/bin/BiglyBTC-headless")
    if runtime is None:
        for dependency in ("libqt6core6t64", "libqt6gui6", "libqt6widgets6", "qt6-qpa-plugins"):
            if not re.search(r"(?:^|,\s*)" + dependency + r" \(>= [0-9]+\.[0-9]+\.[0-9]+\)(?:,|$)", fields["Depends"]):
                raise ValueError(f"Missing explicit Qt runtime minimum: {dependency}")
        if not re.fullmatch(r"qt6-wayland \(>= [0-9]+\.[0-9]+\.[0-9]+\)", fields["Recommends"]):
            raise ValueError("Missing Wayland runtime recommendation")
    else:
        if runtime["variant"] != "bundled-qt" or len(runtime["files"]) > 400:
            raise ValueError("Invalid Qt bundle identity or file count")
        if re.search(r"(?:^|,\s*)(?:libqt6|qt6-|biglybtc-experimental)", fields["Depends"]):
            raise ValueError("Private Qt bundle depends on system Qt or itself")
        allowed.add(runtime_path)
        allowed.add("usr/lib/biglybtc/bin/qt.conf")
        executable_paths = ("usr/lib/biglybtc/bin/BiglyBTC", "usr/lib/biglybtc/bin/BiglyBTC-headless")
        allowed.update(executable_paths)
        for item in runtime["files"]:
            name = item["destination"]
            if not re.fullmatch(
                r"usr/lib/biglybtc/qt/lib/(?:libQt6[A-Za-z0-9]+\.so\.6|libicu(?:data|i18n|uc)\.so\.[0-9]+)"
                r"|usr/lib/biglybtc/qt/plugins/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+\.so"
                r"|usr/lib/biglybtc/qt/translations/qtbase_[A-Za-z_]+\.qm"
                r"|usr/share/doc/biglybtc-experimental/qt/[A-Za-z0-9_.+-]+\.(?:txt|json)", name):
                raise ValueError("Unexpected private runtime path: " + name)
            allowed.add(name)
    control = run("dpkg-deb", "--ctrl-tarfile", str(package))
    with tarfile.open(fileobj=io.BytesIO(control)) as archive:
        control_files = {member.name.removeprefix("./") for member in archive if not member.isdir()}
    if not control_files <= {"control", "md5sums"} or "control" not in control_files:
        raise ValueError("Unexpected maintainer script or control file")
    files = members(raw, allowed)
    if files.keys() != allowed:
        raise ValueError(f"Unexpected payload: {sorted(files.keys() ^ allowed)}")
    if runtime:
        for application in ("BiglyBTC", "BiglyBTC-headless"):
            expected = f'#!/bin/sh\nexec /usr/lib/biglybtc/bin/{application} "$@"\n'.encode()
            if files["usr/bin/" + application] != expected:
                raise ValueError("Unexpected bundled runtime launcher")
        if files["usr/lib/biglybtc/bin/qt.conf"] != b"[Paths]\nPrefix=../qt\nLibraries=lib\nPlugins=plugins\nTranslations=translations\n":
            raise ValueError("Unexpected private Qt configuration")
    dynamic = {}
    with tempfile.TemporaryDirectory(prefix="biglybtc-deb-audit-", dir="/tmp") as temporary:
        elf_paths = set(executable_paths) | {name for name in files if name.startswith("usr/lib/biglybtc/qt/") and ".so" in name}
        for name in sorted(elf_paths):
            data = files[name]
            if not data.startswith(b"\x7fELF"):
                raise ValueError(f"Expected ELF executable: {name}")
            executable = pathlib.Path(temporary) / pathlib.PurePosixPath(name).name
            executable.write_bytes(data)
            details = run("readelf", "--dynamic", str(executable)).decode()
            paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[([^\]]*)\]", details)
            expected_path = "$ORIGIN/../qt/lib" if name in executable_paths else (
                "$ORIGIN/../../lib" if "/plugins/" in name else "$ORIGIN")
            if (not runtime and paths) or (runtime and any(path != expected_path for path in paths)):
                raise ValueError(f"Installed ELF retains an unexpected runtime path: {name}")
            needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", details)
            if (not needed and name in executable_paths) or any("/" in library for library in needed):
                raise ValueError(f"Expected system shared-library names: {name}")
            dynamic[name] = needed
    return {
        "scope": "experimental packaging contract, not runtime or install qualification",
        "package_sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
        "bytes": package.stat().st_size,
        "control": fields,
        "payload_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
        "needed": dynamic,
        "variant": "bundled-qt" if runtime else "system-qt",
        "bundled_dependencies": ["Qt", "Qt SDK ICU runtime"] if runtime else [],
        "installed_or_executed": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    report = json.dumps(audit(args.package.resolve()), indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    print(report, end="")


if __name__ == "__main__":
    main()
