#!/usr/bin/env python3
"""Inspect a locally built experimental DEB without installing or executing it."""

import argparse
import hashlib
import io
import json
import pathlib
import re
import struct
import subprocess
import tarfile
import tempfile


PAYLOAD = {
    "usr/bin/BiglyBTC",
    "usr/bin/BiglyBTC-headless",
    "usr/share/doc/biglybtc/copyright",
    "usr/share/doc/biglybtc/EXPERIMENTAL.txt",
    "usr/share/applications/BiglyBTC.desktop",
}
APPLICATION_MODULES = {
    "usr/lib/biglybtc/lib/BiglyBTC-plugin-azlocprov.so",
    "usr/lib/biglybtc/lib/BiglyBTC-plugin-SpeedScheduler.so",
}
RELEASE_SDK = "usr/lib/biglybtc/lib/libtorrent-rasterbar.so.2.0"
RELEASE_SDK_RECEIPT = "usr/share/doc/biglybtc/release-sdk.json"
RELEASE_GEOIP = {
    "usr/share/BiglyBTC/geoip/DBIP-Country.mmdb": "145ce5dfb812d8de28547e6289bb5c5f8b3df2f1dea21d324d5b575e0431311f",
    "usr/share/BiglyBTC/geoip/DBIP-ASN.mmdb": "81565a450281c949fc83745f5ec9cfea70b035163b1152b055645884765d153d",
}
RELEASE_PAYLOAD = {RELEASE_SDK, RELEASE_SDK_RECEIPT, *RELEASE_GEOIP,
                   "usr/share/BiglyBTC/geoip/ATTRIBUTION.txt", "usr/share/doc/biglybtc/libtorrent-copyright"}
TRIAL = "usr/lib/biglybtc/libexec/biglybtc-plugin-release-trial"
BUNDLED_APPLICATION_LIBRARIES = {
    "usr/lib/biglybtc/lib/libmaxminddb.so.0",
    "usr/lib/biglybtc/lib/libarchive.so.13",
}
BUNDLED_APPLICATION_NOTICES = {
    "usr/share/doc/biglybtc/libmaxminddb-copyright",
    "usr/share/doc/biglybtc/libarchive-copyright",
}
APPLICATION_ICONS = {
    f"usr/share/icons/hicolor/{size}x{size}/apps/biglybtc.png": size
    for size in (16, 32, 48, 64, 128, 256)
}
APPLICATION_PAYLOAD = APPLICATION_MODULES | set(APPLICATION_ICONS) | {TRIAL,
    "usr/share/doc/biglybtc/SpeedScheduler/LICENSE",
    "usr/share/doc/biglybtc/SpeedScheduler/NOTICE.md",
    "usr/share/doc/biglybtc/SpeedScheduler/SOURCE_PROVENANCE.json",
}
MAX_ARCHIVE = 128 * 1024 * 1024
ELF_MACHINE = {"amd64": 62, "arm64": 183}


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


def audit(package, application_payload=False, expected_architecture=None, release_sdk=None):
    if package.stat().st_size > MAX_ARCHIVE:
        raise ValueError("Package exceeds the experimental archive bound")
    fields = {}
    for field in ("Package", "Version", "Architecture", "Depends", "Recommends",
                  "Conflicts", "Replaces", "Provides"):
        fields[field] = run("dpkg-deb", "--field", str(package), field).decode().strip()
    architecture = fields["Architecture"]
    if fields["Package"] != "biglybtc" or architecture not in ELF_MACHINE:
        raise ValueError("Unexpected package identity or architecture")
    for relation in ("Conflicts", "Replaces"):
        if fields[relation] != "biglybtc-experimental":
            raise ValueError(f"Missing legacy package transition field: {relation}")
    if fields["Provides"] != f"biglybtc-experimental (= {fields['Version']})":
        raise ValueError("Missing versioned legacy package provider")
    if expected_architecture is not None and architecture != expected_architecture:
        raise ValueError(f"Expected package architecture {expected_architecture}, got {architecture}")
    if not re.fullmatch(r"[0-9][A-Za-z0-9.+]*~dev(?:\.[1-9][0-9]*)?", fields["Version"]):
        raise ValueError("Expected an explicitly experimental development version")
    raw = run("dpkg-deb", "--fsys-tarfile", str(package))
    runtime_path = "usr/share/doc/biglybtc/qt-runtime.json"
    runtime = None
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for member in archive:
            if member.name.removeprefix("./") == runtime_path:
                if not member.isfile() or member.size > 1024 * 1024:
                    raise ValueError("Invalid private runtime manifest")
                runtime = json.loads(archive.extractfile(member).read())
                break
    allowed = PAYLOAD | (APPLICATION_PAYLOAD if application_payload else set())
    if release_sdk:
        allowed |= RELEASE_PAYLOAD
        if "libtorrent-rasterbar2.0t64" in fields["Depends"]:
            raise ValueError("Release package depends on the unextended system torrent engine")
    executable_paths = ("usr/bin/BiglyBTC", "usr/bin/BiglyBTC-headless")
    if runtime is None:
        for dependency in ("libqt6core6t64", "libqt6gui6", "libqt6widgets6", "qt6-qpa-plugins"):
            if not re.search(r"(?:^|,\s*)" + dependency + r" \(>= [0-9]+\.[0-9]+\.[0-9]+\)(?:,|$)", fields["Depends"]):
                raise ValueError(f"Missing explicit Qt runtime minimum: {dependency}")
        if not re.search(r"(?:^|,\s*)qt6-wayland \(>= [0-9]+\.[0-9]+\.[0-9]+\)(?:,|$)",
                         fields["Recommends"]):
            raise ValueError("Missing Wayland runtime recommendation")
        if application_payload:
            system_libraries = ("libarchive13t64", "libmaxminddb0")
            if not release_sdk:
                system_libraries += ("libtorrent-rasterbar2.0t64",)
            for dependency in system_libraries:
                if not re.search(r"(?:^|,\s*)" + re.escape(dependency) + r"(?: \(>= [^)]+\))?(?:,|$)",
                                 fields["Depends"]):
                    raise ValueError(f"Missing system shared-library dependency: {dependency}")
    else:
        if application_payload:
            allowed.update(BUNDLED_APPLICATION_LIBRARIES | BUNDLED_APPLICATION_NOTICES)
        if runtime["variant"] != "bundled-qt" or len(runtime["files"]) > 400:
            raise ValueError("Invalid Qt bundle identity or file count")
        if re.search(r"(?:^|,\s*)(?:libqt6|qt6-|biglybtc)", fields["Depends"]):
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
                r"|usr/share/doc/biglybtc/qt/[A-Za-z0-9_.+-]+\.(?:txt|json)", name):
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
    if release_sdk:
        receipt = json.loads(release_sdk.read_bytes())
        if (receipt != json.loads(files[RELEASE_SDK_RECEIPT]) or receipt["machine"] != ELF_MACHINE[architecture]
                or len(files[RELEASE_SDK]) != receipt["bytes"]
                or hashlib.sha256(files[RELEASE_SDK]).hexdigest() != receipt["sha256"]):
            raise ValueError("Packaged SDK differs from the verified build image")
        for name, digest in RELEASE_GEOIP.items():
            if hashlib.sha256(files[name]).hexdigest() != digest:
                raise ValueError(f"Unexpected release database contents: {name}")
    desktop_entry = files["usr/share/applications/BiglyBTC.desktop"].splitlines()
    if b"Icon=biglybtc" not in desktop_entry or b"Exec=BiglyBTC" not in desktop_entry:
        raise ValueError("Unexpected desktop launcher for BiglyBTC")
    if application_payload:
        for name, size in APPLICATION_ICONS.items():
            header = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + struct.pack(">II", size, size)
            if not files[name].startswith(header):
                raise ValueError(f"Invalid {size}x{size} application icon: {name}")
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
        if release_sdk:
            elf_paths.add(RELEASE_SDK)
        if application_payload:
            elf_paths.update(APPLICATION_MODULES | {TRIAL})
            if runtime:
                elf_paths.update(BUNDLED_APPLICATION_LIBRARIES)
        for name in sorted(elf_paths):
            data = files[name]
            if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
                raise ValueError(f"Expected a little-endian ELF64 executable: {name}")
            if struct.unpack_from("<H", data, 18)[0] != ELF_MACHINE[architecture]:
                raise ValueError(f"ELF architecture does not match the package: {name}")
            executable = pathlib.Path(temporary) / pathlib.PurePosixPath(name).name
            executable.write_bytes(data)
            details = run("readelf", "--dynamic", str(executable)).decode()
            paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[([^\]]*)\]", details)
            if name in executable_paths:
                expected_path = "$ORIGIN/../lib:$ORIGIN/../qt/lib" if runtime else "$ORIGIN/../lib/biglybtc/lib"
                if paths != [expected_path]:
                    raise ValueError(f"Application does not resolve bundled libraries: {name}")
            else:
                expected_path = "$ORIGIN/../../lib" if "/plugins/" in name else "$ORIGIN"
                if name == TRIAL:
                    expected_path = None
                if any(path != expected_path for path in paths):
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
        "bundled_dependencies": (
            ["Qt", "Qt SDK ICU runtime"] + (["libmaxminddb", "libarchive"] if application_payload else [])
            if runtime else []
        ) + (["BiglyBTC torrent SDK"] if release_sdk else []),
        "installed_or_executed": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--application", action="store_true")
    parser.add_argument("--architecture", choices=ELF_MACHINE)
    parser.add_argument("--release-sdk", type=pathlib.Path)
    args = parser.parse_args()
    report = json.dumps(audit(args.package.resolve(), args.application, args.architecture, args.release_sdk), indent=2) + "\n"
    if args.output:
        args.output.write_text(report)
    print(report, end="")


if __name__ == "__main__":
    main()
