#!/usr/bin/env python3
"""Bind the daily's source inventory to the native SDK built for a release."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


DAILY_MANIFEST_SHA256 = "a5448526308d37a23afd18464b179e7fee5c36669f7d6353a306fc8c864e9766"
ELF_MACHINE = {"x86_64": 62, "amd64": 62, "aarch64": 183, "arm64": 183}
SONAME = "libtorrent-rasterbar.so.2.0"
MACHO_CPU = {"x86_64": 0x01000007, "amd64": 0x01000007, "aarch64": 0x0100000C, "arm64": 0x0100000C}
MACHO_INSTALL_NAME = "@rpath/libtorrent-rasterbar.2.0.dylib"
LINUX_POP_ALERTS = "_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE"
MACOS_POP_ALERTS = "_ZN10libtorrent14session_handle10pop_alertsEPNSt3__16vectorIPNS_5alertENS1_9allocatorIS4_EEEE"
WINDOWS_DLL = "torrent-rasterbar.dll"
WINDOWS_EXPORTS = {
    LINUX_POP_ALERTS: "?pop_alerts@session_handle@libtorrent@@",
    "_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE":
        "?remove_torrent@session_handle@libtorrent@@",
    "_ZN10libtorrent14session_handle18post_session_statsEv": "?post_session_stats@session_handle@libtorrent@@",
    "_ZN10libtorrent21session_stats_metricsEv": "?session_stats_metrics@libtorrent@@",
    "_ZN10libtorrent7versionEv": "?version@libtorrent@@",
    "_ZNK10libtorrent19session_stats_alert8countersEv": "?counters@session_stats_alert@libtorrent@@",
}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_tree(root, expected):
    files = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Unexpected symlink in SDK inputs: {path}")
        if path.is_file():
            files[path.relative_to(root).as_posix()] = path
    if files.keys() != expected.keys():
        raise ValueError(f"Unexpected SDK input inventory: {sorted(files.keys() ^ expected.keys())}")
    for name, path in files.items():
        if sha256(path) != expected[name]:
            raise ValueError(f"Unexpected SDK input contents: {name}")


def write_header(path, flags, namespace, values, size, exports=()):
    lines = ["#pragma once", *(f"#define BIGLYBTC_{flag} 1" for flag in flags),
             f"namespace {namespace} {{"]
    lines.extend(f"inline constexpr char {key}[] = {json.dumps(value)};" for key, value in values.items())
    lines.append(f"inline constexpr unsigned long long bytes = {size}ULL;")
    if exports:
        lines.append("inline constexpr const char* exports[] = {")
        lines.extend(f"    {json.dumps(name)}," for name in exports)
        lines.append("};")
    lines.extend(("}", ""))
    path.write_text("\n".join(lines))


def elf_details(library, header, processor):
    machine = ELF_MACHINE[processor]
    if header[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", header, 16) != (3, machine):
        raise ValueError("The release SDK must be a native little-endian ELF64 shared library")
    dynamic = subprocess.check_output(["readelf", "--dynamic", str(library)], text=True)
    if re.findall(r"\(SONAME\).*\[([^\]]+)\]", dynamic) != [SONAME] or re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        raise ValueError("Unexpected release SDK loader metadata")
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(library)], text=True)
    exports = {fields[7].split("@")[0] for line in symbols.splitlines() if len(fields := line.split()) >= 8
               and fields[0].rstrip(":").isdigit() and fields[6] != "UND"}
    return SONAME, machine, exports


def macho_details(library, header, processor):
    machine = MACHO_CPU[processor]
    if header[:4] != b"\xcf\xfa\xed\xfe" or struct.unpack_from("<I", header, 4)[0] != machine \
            or struct.unpack_from("<I", header, 12)[0] != 6:  # MH_DYLIB
        raise ValueError("The release SDK must be a native little-endian Mach-O 64-bit dylib")
    names = subprocess.check_output(["otool", "-D", str(library)], text=True).splitlines()[1:]
    commands = subprocess.check_output(["otool", "-l", str(library)], text=True)
    if names != [MACHO_INSTALL_NAME] or re.search(r"^\s*cmd LC_RPATH\s*$", commands, re.MULTILINE):
        raise ValueError("Unexpected Mach-O release SDK loader metadata")
    symbols = subprocess.check_output(["nm", "-gjU", str(library)], text=True)
    exports = {name.removeprefix("_") for line in symbols.splitlines() if (name := line.strip()).startswith("_")}
    return MACHO_INSTALL_NAME, machine, exports


def verify_sdk(args, daily):
    library = args.library.resolve(strict=True)
    size = library.stat().st_size
    with library.open("rb") as stream:
        header = stream.read(64)
    if size > 64 * 1024 * 1024 or len(header) != 64:
        raise ValueError("Unexpected release SDK file size")
    if args.platform == "windows":
        from pe_exports import dll_exports

        name, machine, exports = dll_exports(library, args.processor)
        if name.casefold() != WINDOWS_DLL or library.name.casefold() != WINDOWS_DLL:
            raise ValueError("Unexpected torrent SDK DLL name")
    else:
        inspect = macho_details if args.platform == "macos" else elf_details
        name, machine, exports = inspect(library, header, args.processor)
    verify_tree(args.include / "libtorrent",
                {name.removeprefix("libtorrent/"): sha for name, sha in daily["headers_sha256"].items()})
    required = daily["required_exports"]
    if args.platform == "macos":
        # The installed Apple SDK uses libc++ for its public vector argument.
        required = [MACOS_POP_ALERTS if name == LINUX_POP_ALERTS else name for name in required]
    if args.platform == "windows":
        selected = []
        for symbol in required:
            prefix = WINDOWS_EXPORTS.get(symbol)
            if prefix is None or symbol in exports:
                selected.append(symbol)
                continue
            matches = sorted(name for name in exports if name.startswith(prefix))
            if len(matches) != 1:
                raise ValueError(f"Missing or ambiguous MSVC export: {prefix}")
            selected.append(matches[0])
        required = selected
    missing = set(required) - exports
    if missing:
        raise ValueError(f"Missing daily SDK exports: {sorted(missing)}")

    digest = sha256(library)
    values = dict(artifact=str(library), sha256=digest, manifest=DAILY_MANIFEST_SHA256,
                  source_capsule_sha256=daily["source_inventory_sha256"], sdk_tree_sha256=daily["sdk_tree_sha256"])
    write_header(args.header, (
        "RELEASE_RUNTIME_BOUND", "PERSISTENT_RUNTIME_BOUND", "PUBLIC_SHARED_CHAT_BOUND",
        "PUBLIC_TAG_RATES_BOUND", "PUBLIC_SHARED_QUOTA_BOUND", "SHARED_QUOTA_ALL_ROLES_BOUND",
        "PERSISTENT_MIXED_I2P_BOUND", "ORIGINAL_RATE_POLICY_BOUND",
    ), "biglybtc::engine::runtime_binding", values, size, required if args.platform == "windows" else ())
    write_header(args.private_header, (
        "PRIVATE_RELEASE_RUNTIME_BOUND", "PRIVATE_MIXED_ROLE_BOUND",
        "PRIVATE_SHARED_QUOTA_BOUND", "PRIVATE_ORIGINAL_RATE_POLICY_BOUND",
    ), "biglybtc::services::i2p::provider::private_i2p_runtime_binding", {"sha256": digest}, size)

    args.receipt.write_text(json.dumps(dict(
        soname=name, sha256=digest, bytes=size, machine=machine,
        source_manifest_sha256=DAILY_MANIFEST_SHA256, source_inventory_sha256=daily["source_inventory_sha256"],
        sdk_tree_sha256=daily["sdk_tree_sha256"], roles=daily["roles"], required_exports=required,
    ), indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--processor", choices=ELF_MACHINE)
    parser.add_argument("--platform", choices=("linux", "macos", "windows"), default="linux")
    for name in ("include", "header", "private-header", "receipt"):
        parser.add_argument(f"--{name}", type=Path)
    args = parser.parse_args()
    manifest = Path(__file__).with_name("provider-original-rate-policy-runtime.json")
    if sha256(manifest) != DAILY_MANIFEST_SHA256:
        raise ValueError("Unexpected daily SDK source manifest")
    daily = json.loads(manifest.read_text())
    verify_tree(args.source, daily["source_inputs_sha256"])
    if args.library:
        if not all((args.processor, args.include, args.header, args.private_header, args.receipt)):
            parser.error("SDK binding requires target, include directory and output paths")
        verify_sdk(args, daily)


if __name__ == "__main__":
    main()
