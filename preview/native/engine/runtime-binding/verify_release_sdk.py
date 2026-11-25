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


def write_header(path, flags, namespace, values, size):
    lines = ["#pragma once", *(f"#define BIGLYBTC_{flag} 1" for flag in flags),
             f"namespace {namespace} {{"]
    lines.extend(f"inline constexpr char {key}[] = {json.dumps(value)};" for key, value in values.items())
    lines.extend((f"inline constexpr unsigned long long bytes = {size}ULL;", "}", ""))
    path.write_text("\n".join(lines))


def verify_sdk(args, daily):
    library = args.library.resolve(strict=True)
    size = library.stat().st_size
    with library.open("rb") as stream:
        elf = stream.read(64)
    if (size > 64 * 1024 * 1024 or len(elf) != 64 or elf[:7] != b"\x7fELF\x02\x01\x01"
            or struct.unpack_from("<HH", elf, 16) != (3, ELF_MACHINE[args.processor])):
        raise ValueError("The release SDK must be a native little-endian ELF64 shared library")

    verify_tree(args.include / "libtorrent",
                {name.removeprefix("libtorrent/"): sha for name, sha in daily["headers_sha256"].items()})
    dynamic = subprocess.check_output(["readelf", "--dynamic", str(library)], text=True)
    if re.findall(r"\(SONAME\).*\[([^\]]+)\]", dynamic) != [SONAME] or re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        raise ValueError("Unexpected release SDK loader metadata")
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(library)], text=True)
    exports = {fields[7].split("@")[0] for line in symbols.splitlines() if len(fields := line.split()) >= 8
               and fields[0].rstrip(":").isdigit() and fields[6] != "UND"}
    missing = set(daily["required_exports"]) - exports
    if missing:
        raise ValueError(f"Missing daily SDK exports: {sorted(missing)}")

    digest = sha256(library)
    values = dict(artifact=str(library), sha256=digest, manifest=DAILY_MANIFEST_SHA256,
                  source_capsule_sha256=daily["source_inventory_sha256"], sdk_tree_sha256=daily["sdk_tree_sha256"])
    write_header(args.header, (
        "RELEASE_RUNTIME_BOUND", "PERSISTENT_RUNTIME_BOUND", "PUBLIC_SHARED_CHAT_BOUND",
        "PUBLIC_TAG_RATES_BOUND", "PUBLIC_SHARED_QUOTA_BOUND", "SHARED_QUOTA_ALL_ROLES_BOUND",
        "PERSISTENT_MIXED_I2P_BOUND", "ORIGINAL_RATE_POLICY_BOUND",
    ), "biglybtc::engine::runtime_binding", values, size)
    write_header(args.private_header, (
        "PRIVATE_MIXED_ROLE_BOUND", "PRIVATE_SHARED_QUOTA_BOUND", "PRIVATE_ORIGINAL_RATE_POLICY_BOUND",
    ), "biglybtc::services::i2p::provider::private_i2p_runtime_binding", {"sha256": digest}, size)

    args.receipt.write_text(json.dumps(dict(
        soname=SONAME, sha256=digest, bytes=size, machine=ELF_MACHINE[args.processor],
        source_manifest_sha256=DAILY_MANIFEST_SHA256, source_inventory_sha256=daily["source_inventory_sha256"],
        sdk_tree_sha256=daily["sdk_tree_sha256"], roles=daily["roles"], required_exports=daily["required_exports"],
    ), indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--processor", choices=ELF_MACHINE)
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
