#!/usr/bin/env python3
"""Plan a private Qt runtime from the configured SDK; never copy the SDK wholesale."""

import argparse
import hashlib
import json
import pathlib
import re
import subprocess

PRIVATE = "usr/lib/biglybtc/qt"
MODULES = ("qtbase", "qtwayland", "qtimageformats", "qtsvg", "qttranslations")
PLUGIN_TYPES = ("imageformats", "iconengines", "platformthemes", "xcbglintegrations",
    "wayland-decoration-client", "wayland-graphics-integration-client", "wayland-shell-integration")


def dynamic(path):
    output = subprocess.run(["readelf", "--dynamic", str(path)], check=True,
        capture_output=True, text=True, timeout=30).stdout
    return {
        "needed": re.findall(r"\(NEEDED\).*\[([^\]]+)\]", output),
        "soname": re.findall(r"\(SONAME\).*\[([^\]]+)\]", output),
        "rpath": re.findall(r"\((?:RUNPATH|RPATH)\).*\[([^\]]*)\]", output),
    }


def plan(sdk, version, notices):
    sdk = sdk.resolve(strict=True)
    files = {}
    queue = []

    def add(source, destination, elf=False):
        source = source.resolve(strict=True)
        if source.is_relative_to(sdk) is False and not source.is_relative_to(notices.resolve()):
            raise ValueError("Runtime input escaped its SDK/notices directory")
        info = {"source": str(source), "destination": destination,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
        if elf:
            info.update(dynamic(source))
            expected = "$ORIGIN/../../lib" if "/plugins/" in destination else "$ORIGIN"
            if any(path != expected for path in info["rpath"]):
                raise ValueError(f"Unqualified SDK runtime path: {source}")
            queue.extend(info["needed"])
        files[destination] = info

    for plugin in ("libqxcb.so", "libqwayland.so", "libqoffscreen.so", "libqminimal.so"):
        add(sdk / "plugins/platforms" / plugin, f"{PRIVATE}/plugins/platforms/{plugin}", True)
    for plugin_type in PLUGIN_TYPES:
        for plugin in sorted((sdk / "plugins" / plugin_type).glob("*.so")):
            add(plugin, f"{PRIVATE}/plugins/{plugin_type}/{plugin.name}", True)
    queue.extend(("libQt6Core.so.6", "libQt6Gui.so.6", "libQt6Widgets.so.6"))
    seen = set()
    while queue:
        name = queue.pop()
        if name in seen:
            continue
        seen.add(name)
        # ICU supplied with this Qt SDK is part of the private Qt runtime closure.
        # The current host's newer ICU cannot satisfy the SDK's ICU73 SONAMEs.
        if re.fullmatch(r"libQt6[A-Za-z0-9]+\.so\.6|libicu(?:data|i18n|uc)\.so\.[0-9]+", name):
            add(sdk / "lib" / name, f"{PRIVATE}/lib/{name}", True)
    for translation in sorted((sdk / "translations").glob("qtbase_*.qm")):
        add(translation, f"{PRIVATE}/translations/{translation.name}")
    for module in MODULES:
        sbom = sdk / "sbom" / (module + "-" + version + ".spdx.json")
        add(sbom, "usr/share/doc/biglybtc-experimental/qt/" + sbom.name)
    for notice in sorted(notices.glob("*.txt")):
        add(notice, "usr/share/doc/biglybtc-experimental/qt/" + notice.name)
    for required in ("LGPL-3.0-only.txt", "GPL-3.0-only.txt", "ICU-LICENSE.txt", "Qt-runtime.txt"):
        if not (notices / required).is_file():
            raise ValueError("Missing bundled runtime notice: " + required)
    return {"variant": "bundled-qt", "qt_version": version,
        "private_runtime": PRIVATE, "files": list(files.values()),
        "system_sonames": sorted(seen - {pathlib.PurePosixPath(p).name for p in files}),
        "qualification": "experimental; SDK source/third-party redistribution closure remains pending"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=pathlib.Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--notices", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(plan(args.sdk, args.version, args.notices), indent=2) + "\n")


if __name__ == "__main__":
    main()
