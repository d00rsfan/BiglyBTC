#!/usr/bin/env python3
"""Stage, audit and package a relocatable BiglyBTC macOS application."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


SDK_NAME = "libtorrent-rasterbar.2.0.dylib"
BUNDLE_RUNTIME = "@executable_path/../Frameworks"


def run(*command):
    return subprocess.check_output(command, text=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require_sdk(path, receipt):
    if receipt["soname"] != f"@rpath/{SDK_NAME}" or path.stat().st_size != receipt["bytes"]:
        raise ValueError("Unexpected release SDK identity")
    if digest(path) != receipt["sha256"]:
        raise ValueError("Release SDK bytes changed")


def runtime_paths(binary):
    lines = [line.strip() for line in run("/usr/bin/otool", "-l", binary).splitlines()]
    return [lines[i + 2].removeprefix("path ").rsplit(" (offset ", 1)[0]
            for i, line in enumerate(lines) if line == "cmd LC_RPATH"]


def prepare_executable(binary):
    paths = runtime_paths(binary)
    for path in sorted(set(paths) - {BUNDLE_RUNTIME}):
        run("/usr/bin/install_name_tool", "-delete_rpath", path, binary)
    if BUNDLE_RUNTIME not in paths:
        run("/usr/bin/install_name_tool", "-add_rpath", BUNDLE_RUNTIME, binary)


def audit_dependencies(binary, app):
    frameworks = app / "Contents" / "Frameworks"
    executable = app / "Contents" / "MacOS"
    prefixes = {
        "@rpath": frameworks,
        "@loader_path": binary.parent,
        "@executable_path": executable,
    }
    for path in runtime_paths(binary):
        prefix, _, leaf = path.partition("/")
        if prefix not in ("@loader_path", "@executable_path") or not (prefixes[prefix] / leaf).resolve().is_relative_to(app):
            raise ValueError(f"{binary.relative_to(app)} has a runtime search path outside the bundle: {path}")
    for line in run("/usr/bin/otool", "-L", binary).splitlines():
        if not line.startswith("\t"):
            continue
        name = line.strip().split(" (compatibility version", 1)[0]
        if name.startswith(("/usr/lib/", "/System/Library/")):
            continue
        prefix, _, leaf = name.partition("/")
        if prefix in prefixes and leaf:
            target = (prefixes[prefix] / leaf).resolve()
            if target.is_relative_to(app) and target.is_file():
                continue
        raise ValueError(f"{binary.relative_to(app)} depends on an external or missing library: {name}")


def framework_binary(path):
    binary = (path / path.stem).resolve(strict=True)
    if not binary.is_relative_to(path):
        raise ValueError(f"Unexpected framework executable location: {path}")
    return binary


def stage_resources(app, geoip, receipt, sdk_license):
    resources = app / "Contents" / "Resources"
    data = resources / "geoip"
    data.mkdir()
    data.chmod(0o755)
    for name in ("DBIP-Country.mmdb", "DBIP-ASN.mmdb"):
        target = data / name
        shutil.copyfile(geoip / name, target)
        target.chmod(0o644)
    attribution = Path(__file__).with_name("notices") / "db-ip.txt"
    shutil.copyfile(attribution, data / "ATTRIBUTION.txt")
    shutil.copyfile(receipt, resources / "release-sdk.json")
    shutil.copyfile(sdk_license, resources / "libtorrent-copyright")


def package(args):
    receipt = json.loads(args.receipt.read_text())
    sdk = args.sdk.resolve(strict=True)
    require_sdk(sdk, receipt)
    run("/usr/bin/codesign", "--verify", sdk)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="biglybtc-mac-", dir=args.output.parent) as temporary:
        work = Path(temporary).resolve()
        image = work / "payload"
        image.mkdir()
        app = image / "BiglyBTC.app"
        shutil.copytree(args.bundle, app, symlinks=True)
        stage_resources(app, args.geoip, args.receipt, args.sdk_license)
        frameworks = app / "Contents" / "Frameworks"
        frameworks.mkdir(exist_ok=True)
        bundled_sdk = frameworks / SDK_NAME
        shutil.copy2(sdk, bundled_sdk)
        run(args.macdeployqt, app, "-no-strip", "-no-codesign", f"-libpath={sdk.parent}")
        require_sdk(bundled_sdk, receipt)
        executable = app / "Contents" / "MacOS" / "BiglyBTC"
        prepare_executable(executable)
        code = sorted(path for path in app.rglob("*.dylib") if not path.is_symlink())
        bundles = sorted(path for path in frameworks.rglob("*.framework") if not path.is_symlink())
        for binary in [executable, *code, *(framework_binary(path) for path in bundles)]:
            audit_dependencies(binary, app)
        for path in [*(path for path in code if path != bundled_sdk), *reversed(bundles), app]:
            run("/usr/bin/codesign", "--force", "--sign", "-", path)
        run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
        require_sdk(bundled_sdk, receipt)
        (image / "Applications").symlink_to("/Applications")
        result = work / "BiglyBTC.dmg"
        run("/usr/bin/hdiutil", "create", "-volname", "BiglyBTC", "-srcfolder", image,
            "-format", "UDZO", result)
        run("/usr/bin/hdiutil", "verify", result)
        os.replace(result, args.output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("bundle", "sdk", "receipt", "sdk-license", "geoip", "macdeployqt", "output"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    package(parser.parse_args())
