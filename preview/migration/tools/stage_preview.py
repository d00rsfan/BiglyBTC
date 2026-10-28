#!/usr/bin/env python3
"""Stage a directly runnable development folder, without creating or installing a DEB."""

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess


# This is the retained, uninstrumented GCC16 source build, never a causal-test
# provider. A pathname or matching version alone cannot authorize another DSO.
SOURCE_LIBTORRENT_SHA256 = "c35f1b0408508c019f002c438d70dd361a55be20286ded56711efd6bba2424e3"
SOURCE_LIBTORRENT_BYTES = 8737560
SOURCE_LIBTORRENT_COMMIT = "740a0b9aeabe00e762cc0efe4a0f27593db2550b"
SOURCE_BUILD_RECORD_SHA256 = "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66"
LIBTORRENT_NAMES = ("libtorrent-rasterbar.so", "libtorrent-rasterbar.so.2.0",
                    "libtorrent-rasterbar.so.2.0.12")

# Independent explicit lane; the original c35 selector and all instrumented
# denials below remain unchanged. No version/capability-only authorization.
SELECTED_LIBTORRENT_SHA256 = "d367afd6499859c5a57d66bddd352a977ff76c7fb76b5925302c2c9008c7ddbd"
SELECTED_LIBTORRENT_BYTES = 9072560
SELECTED_RUNTIME_MANIFEST_SHA256 = "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737"
SELECTED_BUILD_RECORD_SHA256 = "34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f"
SELECTED_VERIFIERS = {
    "verify_runtime_manifest.py": "db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c",
    "verify_provider_runtime.py": "fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a",
}


def source_libtorrent(path):
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or resolved.stat().st_size != SOURCE_LIBTORRENT_BYTES:
        raise ValueError("Expected the exact uninstrumented c35f libtorrent source runtime")
    payload = resolved.read_bytes()
    if (len(payload) != SOURCE_LIBTORRENT_BYTES or
            hashlib.sha256(payload).hexdigest() != SOURCE_LIBTORRENT_SHA256):
        raise ValueError("Source libtorrent SHA-256 does not match the approved uninstrumented image")
    return resolved, payload


def selected_source_libtorrent(path, manifest_path, build):
    """Preflight only. No output, loader, subprocess, DSO or owner invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unapproved selected-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, SELECTED_RUNTIME_MANIFEST_SHA256)
    record_path = manifest_path.parent / "provider-source-build-record.json"
    pinned_file(record_path, SELECTED_BUILD_RECORD_SHA256)
    manifest = json.loads(manifest_path.read_bytes())
    source = path.resolve(strict=True)
    if (str(source) != manifest["artifact"] or manifest["artifact_sha256"] != SELECTED_LIBTORRENT_SHA256
            or manifest["artifact_bytes"] != SELECTED_LIBTORRENT_BYTES or source.is_symlink()
            or not source.is_file() or source.stat().st_size != SELECTED_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact reviewed selected-swarm provider")
    payload = source.read_bytes()
    if hashlib.sha256(payload).hexdigest() != SELECTED_LIBTORRENT_SHA256:
        raise ValueError("Selected provider full artifact SHA-256 differs")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    for name, pin in SELECTED_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_source_approval", binding / "verify_runtime_manifest.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(manifest["install_root"]) / "include")
    if pin != SELECTED_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Selected provider failed complete source/build/SDK admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt = json.loads((generated / "configure-receipt.json").read_bytes())
    expected = {"manifest_sha256": SELECTED_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": SELECTED_LIBTORRENT_SHA256, "artifact": str(source),
        "source_files": 449, "full_source_files": 1072, "build_rules": 13,
        "bundled_inputs": 16, "installed_files": 284, "build_files": 4,
        "causal_qualified": False, "purpose": "source-bound-qualification-only",
        "provider_counter_and_swarm_candidate": True}
    if receipt != expected:
        raise ValueError("Application was not configured against this exact provider/SDK")
    header = (generated / "persistent_runtime_config.hpp").read_text()
    required = ("#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1",
        'inline constexpr char artifact[] = ' + json.dumps(str(source)) + ';',
        'inline constexpr char sha256[] = "' + SELECTED_LIBTORRENT_SHA256 + '";',
        'inline constexpr char manifest[] = "' + SELECTED_RUNTIME_MANIFEST_SHA256 + '";',
        "inline constexpr unsigned long long bytes = 9072560ULL;")
    if any(text not in header for text in required):
        raise ValueError("Application generated binding does not select this exact provider")
    return source, payload


def install_source_libtorrent(destination, dependency, source, payload):
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or
                    hashlib.sha256(alias.read_bytes()).hexdigest() != SOURCE_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias does not resolve to the exact source image: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {
        "provider": "source-built-uninstrumented", "version": "2.0.12",
        "artifact_sha256": SOURCE_LIBTORRENT_SHA256, "artifact_bytes": SOURCE_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": SOURCE_BUILD_RECORD_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False,
        "scope": "Exact uninstrumented source-built dependency; not full original-client parity",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced",
    }


def install_selected_libtorrent(destination, dependency, source, payload):
    # Called only after complete admission above, before any loader check.
    # Keep the original install_source_libtorrent implementation byte-exact.
    if len(payload) != SELECTED_LIBTORRENT_BYTES or hashlib.sha256(payload).hexdigest() != SELECTED_LIBTORRENT_SHA256:
        raise ValueError("Changed selected-provider bytes before private installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if alias.resolve(strict=True) != canonical or hashlib.sha256(alias.read_bytes()).hexdigest() != SELECTED_LIBTORRENT_SHA256:
                raise ValueError("Runtime alias does not resolve to the exact selected provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-selected-swarm-counter-v1", "version": "2.0.12",
        "artifact_sha256": SELECTED_LIBTORRENT_SHA256, "artifact_bytes": SELECTED_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": SELECTED_BUILD_RECORD_SHA256,
        "runtime_manifest_sha256": SELECTED_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False,
        "scope": "Exact source-built selected-swarm/counter dependency; source binding alone is not runtime or full client parity",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


# Additional explicit PRIVATE qualification route. The public/default distro,
# c35 and e74/d367 selectors below are unchanged. This is not shipping approval.
PRIVATE_NEXT_LIBTORRENT_SHA256 = "b0e55bfbdc2be5dc2ebbefc953837d64c9e5151b0305332335071e37b6956937"
PRIVATE_NEXT_LIBTORRENT_BYTES = 9085072
PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256 = "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"
PRIVATE_NEXT_CANDIDATE = "/home/yurii/Java/BiglyBT/build/storage-20261025/provider-next"
PRIVATE_NEXT_RECORDS = {
    "provider-next-source-build-record.json": "3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca",
    "provider-source-build-record.json": "34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f",
    "source-build-record.json": "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66",
}
PRIVATE_NEXT_VERIFIERS = {
    "verify_runtime_manifest.py": "db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c",
    "verify_provider_next_runtime.py": "0922c9960a507c0b453b240ba3ca5b56c595eaf256fbcf5ce4bcf2d968b88e32",
    "verify_provider_runtime.py": "fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a",
}


def private_next_source_libtorrent(path, manifest_path, build):
    """Read-only exact private preflight; no loader/subprocess/target invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unqualified private-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256)
    for name, pin in PRIVATE_NEXT_RECORDS.items():
        pinned_file(manifest_path.parent / name, pin)
    manifest = json.loads(manifest_path.read_bytes())
    expected_roots = {key: str(Path(PRIVATE_NEXT_CANDIDATE) / name) for key, name in (
        ("source_root", "source"), ("build_root", "build"), ("install_root", "install"))}
    if (any(manifest.get(key) != value for key, value in expected_roots.items())
            or manifest.get("source_build_record") != "provider-next-source-build-record.json"
            or manifest.get("source_build_record_sha256") != PRIVATE_NEXT_RECORDS["provider-next-source-build-record.json"]
            or manifest.get("purpose") != "source-bound-qualification-only"
            or manifest.get("causal_qualified") is not False):
        raise ValueError("Wrong exact private-provider source/history cohort")
    source = path.absolute()
    pinned_file(source, PRIVATE_NEXT_LIBTORRENT_SHA256)
    if (str(source) != manifest.get("artifact")
            or source != Path(expected_roots["install_root"]) / "lib/libtorrent-rasterbar.so.2.0.12"
            or manifest.get("artifact_sha256") != PRIVATE_NEXT_LIBTORRENT_SHA256
            or type(manifest.get("artifact_bytes")) is not int
            or manifest["artifact_bytes"] != PRIVATE_NEXT_LIBTORRENT_BYTES
            or source.stat().st_size != PRIVATE_NEXT_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact privately qualified b0e source artifact")
    payload = source.read_bytes()
    if (len(payload) != PRIVATE_NEXT_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_NEXT_LIBTORRENT_SHA256):
        raise ValueError("Private-provider source image changed during preflight")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    # Both recursive helpers are pinned before executing the outer definitions.
    # No import by ambient module name, path/version fallback or marker trust.
    for name, pin in PRIVATE_NEXT_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_private_next_qualification",
                                                binding / "verify_runtime_manifest.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(expected_roots["install_root"]) / "include")
    if pin != PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Private provider failed complete source/build/SDK/history admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt_path = generated / "configure-receipt.json"
    header_path = generated / "persistent_runtime_config.hpp"
    for candidate in (receipt_path, header_path):
        if candidate.is_symlink() or not candidate.is_file() or candidate.resolve(strict=True) != candidate:
            raise ValueError("Private-provider generated binding is not a canonical regular file")
    receipt = json.loads(receipt_path.read_bytes())
    expected = {"manifest_sha256": PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": PRIVATE_NEXT_LIBTORRENT_SHA256, "artifact": str(source),
        "source_files": 451, "full_source_files": 1074, "build_rules": 13,
        "bundled_inputs": 16, "installed_files": 285, "sdk_headers": 276, "build_files": 4,
        "causal_qualified": False, "purpose": "source-bound-qualification-only",
        "provider_counter_swarm_socks_candidate": True}
    if (receipt != expected or any(type(receipt[key]) is not type(value) for key, value in expected.items())):
        raise ValueError("Application was not configured against this exact private provider/SDK")
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        "inline constexpr char artifact[] = " + json.dumps(str(source)) + ";\n"
        "inline constexpr char sha256[] = " + json.dumps(PRIVATE_NEXT_LIBTORRENT_SHA256) + ";\n"
        "inline constexpr char manifest[] = " + json.dumps(PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256) + ";\n"
        "inline constexpr unsigned long long bytes = 9085072ULL;\n}\n")
    if header_path.read_text() != header:
        raise ValueError("Application compiled binding does not select this exact private provider")
    return source, payload


def install_private_next_libtorrent(destination, dependency, source, payload):
    """Only after exact private preflight, into main's fresh output directory."""
    if (len(payload) != PRIVATE_NEXT_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_NEXT_LIBTORRENT_SHA256):
        raise ValueError("Changed private-provider bytes before installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh private distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh private distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or alias.stat().st_size != PRIVATE_NEXT_LIBTORRENT_BYTES
                    or hashlib.sha256(alias.read_bytes()).hexdigest() != PRIVATE_NEXT_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias differs from the exact private provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-private-socks4a-traffic-v2", "version": "2.0.12",
        "artifact_sha256": PRIVATE_NEXT_LIBTORRENT_SHA256, "artifact_bytes": PRIVATE_NEXT_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": PRIVATE_NEXT_RECORDS["provider-next-source-build-record.json"],
        "parent_source_build_record_sha256": PRIVATE_NEXT_RECORDS["provider-source-build-record.json"],
        "runtime_manifest_sha256": PRIVATE_NEXT_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False, "shipping_approved": False,
        "distribution_scope": "private-qualification-only",
        "scope": "Exact private source/SDK-qualified dependency; application runtime qualification is separate",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


# Independent private webseed selector; requires exact reviewed source/SDK
# admission and the application compiled receipt. No public/default selector.
PRIVATE_WEBSEED_LIBTORRENT_SHA256 = 'd33c17c73c3c377cedd88256b0b48e472b2de55f0a95a890e2837556e1d1002a'
PRIVATE_WEBSEED_LIBTORRENT_BYTES = 9085128
PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256 = 'ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5'
PRIVATE_WEBSEED_CANDIDATE = '/home/yurii/Java/BiglyBT/build/compatibility-20261026/provider-webseed'
PRIVATE_WEBSEED_RECORDS = {'provider-webseed-source-build-record.json': 'a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08', 'provider-next-source-build-record.json': '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca', 'provider-source-build-record.json': '34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f', 'source-build-record.json': '436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66'}
PRIVATE_WEBSEED_VERIFIERS = {'verify_runtime_manifest_webseed.py': 'b07b80c7aa6b0e1371f583eb15051b337cb4d8e96af76c65da5e20bcf6f00ab0', 'verify_runtime_manifest.py': 'db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c', 'verify_provider_next_runtime.py': '0922c9960a507c0b453b240ba3ca5b56c595eaf256fbcf5ce4bcf2d968b88e32', 'verify_provider_runtime.py': 'fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a'}

def private_webseed_source_libtorrent(path, manifest_path, build):
    """Read-only exact private preflight; no loader/subprocess/target invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unqualified private-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256)
    for name, pin in PRIVATE_WEBSEED_RECORDS.items():
        pinned_file(manifest_path.parent / name, pin)
    manifest = json.loads(manifest_path.read_bytes())
    expected_roots = {key: str(Path(PRIVATE_WEBSEED_CANDIDATE) / name) for key, name in (
        ("source_root", "source"), ("build_root", "build"), ("install_root", "install"))}
    if (any(manifest.get(key) != value for key, value in expected_roots.items())
            or manifest.get("source_build_record") != "provider-webseed-source-build-record.json"
            or manifest.get("source_build_record_sha256") != PRIVATE_WEBSEED_RECORDS["provider-webseed-source-build-record.json"]
            or manifest.get("purpose") != "source-bound-qualification-only"
            or manifest.get("causal_qualified") is not False):
        raise ValueError("Wrong exact private-provider source/history cohort")
    source = path.absolute()
    pinned_file(source, PRIVATE_WEBSEED_LIBTORRENT_SHA256)
    if (str(source) != manifest.get("artifact")
            or source != Path(expected_roots["install_root"]) / "lib/libtorrent-rasterbar.so.2.0.12"
            or manifest.get("artifact_sha256") != PRIVATE_WEBSEED_LIBTORRENT_SHA256
            or type(manifest.get("artifact_bytes")) is not int
            or manifest["artifact_bytes"] != PRIVATE_WEBSEED_LIBTORRENT_BYTES
            or source.stat().st_size != PRIVATE_WEBSEED_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact privately qualified d33c source artifact")
    payload = source.read_bytes()
    if (len(payload) != PRIVATE_WEBSEED_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_WEBSEED_LIBTORRENT_SHA256):
        raise ValueError("Private-provider source image changed during preflight")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    # Both recursive helpers are pinned before executing the outer definitions.
    # No import by ambient module name, path/version fallback or marker trust.
    for name, pin in PRIVATE_WEBSEED_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_private_next_qualification",
                                                binding / "verify_runtime_manifest_webseed.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(expected_roots["install_root"]) / "include")
    if pin != PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Private provider failed complete source/build/SDK/history admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt_path = generated / "configure-receipt.json"
    header_path = generated / "persistent_runtime_config.hpp"
    for candidate in (receipt_path, header_path):
        if candidate.is_symlink() or not candidate.is_file() or candidate.resolve(strict=True) != candidate:
            raise ValueError("Private-provider generated binding is not a canonical regular file")
    receipt = json.loads(receipt_path.read_bytes())
    expected = {"manifest_sha256": PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": PRIVATE_WEBSEED_LIBTORRENT_SHA256, "artifact": str(source),
        "source_files": 451, "full_source_files": 1074, "build_rules": 13,
        "bundled_inputs": 16, "installed_files": 285, "sdk_headers": 276, "build_files": 4,
        "causal_qualified": False, "purpose": "source-bound-qualification-only",
        "provider_webseed_candidate": True}
    if (receipt != expected or any(type(receipt[key]) is not type(value) for key, value in expected.items())):
        raise ValueError("Application was not configured against this exact private provider/SDK")
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        "inline constexpr char artifact[] = " + json.dumps(str(source)) + ";\n"
        "inline constexpr char sha256[] = " + json.dumps(PRIVATE_WEBSEED_LIBTORRENT_SHA256) + ";\n"
        "inline constexpr char manifest[] = " + json.dumps(PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256) + ";\n"
        "inline constexpr unsigned long long bytes = 9085128ULL;\n}\n")
    if header_path.read_text() != header:
        raise ValueError("Application compiled binding does not select this exact private provider")
    return source, payload


def install_private_webseed_libtorrent(destination, dependency, source, payload):
    """Only after exact private preflight, into main's fresh output directory."""
    if (len(payload) != PRIVATE_WEBSEED_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_WEBSEED_LIBTORRENT_SHA256):
        raise ValueError("Changed private-provider bytes before installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh private distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh private distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or alias.stat().st_size != PRIVATE_WEBSEED_LIBTORRENT_BYTES
                    or hashlib.sha256(alias.read_bytes()).hexdigest() != PRIVATE_WEBSEED_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias differs from the exact private provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-private-socks4a-webseed-traffic-v2", "version": "2.0.12",
        "artifact_sha256": PRIVATE_WEBSEED_LIBTORRENT_SHA256, "artifact_bytes": PRIVATE_WEBSEED_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": PRIVATE_WEBSEED_RECORDS["provider-webseed-source-build-record.json"],
        "parent_source_build_record_sha256": PRIVATE_WEBSEED_RECORDS["provider-next-source-build-record.json"],
        "runtime_manifest_sha256": PRIVATE_WEBSEED_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False, "shipping_approved": False,
        "distribution_scope": "private-qualification-only",
        "scope": "Exact private source/SDK-qualified dependency; application runtime qualification is separate",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


# Additive exact traffic-V3 lane. Previous selectors/functions stay unchanged.
PRIVATE_TRAFFIC_LIBTORRENT_SHA256 = '98279a468e0fa5147876b6f22f8947f432c294dfc02fb50cf179fc9472b3b51f'
PRIVATE_TRAFFIC_LIBTORRENT_BYTES = 9090088
PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256 = '5901b34eeb25370106bdbf6185a80f0c86c62a837bf2e455f5619921db665760'
PRIVATE_TRAFFIC_CANDIDATE = '/home/yurii/Java/BiglyBT/build/compatibility-chapter-20261027/provider-traffic-v3'
PRIVATE_TRAFFIC_RECORDS = {'provider-traffic-source-build-record.json': '91218c17d3e74bb79a0fea0cfbbf3734e3422895e59f46fad4cb6b17e2927fd0', 'provider-queue-source-build-record.json': '598a2501d1fb3175df4d7fe49e46add045ba78e79c9c0288b7d5b04b4cbaf087', 'provider-webseed-source-build-record.json': 'a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08', 'provider-next-source-build-record.json': '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca', 'provider-source-build-record.json': '34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f', 'source-build-record.json': '436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66'}
PRIVATE_TRAFFIC_VERIFIERS = {'verify_runtime_manifest_traffic.py': '8e37f7ad597ea4d8af5665c35db4466d6e08d367e96380c0c3118454c8b7834b', 'verify_runtime_manifest_queue.py': 'd53a3efa5160764ac48212c0ce0c85aa0fad4e1d1760ea13c3c9f2e2aa6a0bf0', 'verify_runtime_manifest_webseed.py': 'b07b80c7aa6b0e1371f583eb15051b337cb4d8e96af76c65da5e20bcf6f00ab0', 'verify_runtime_manifest.py': 'db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c', 'verify_provider_next_runtime.py': '0922c9960a507c0b453b240ba3ca5b56c595eaf256fbcf5ce4bcf2d968b88e32', 'verify_provider_runtime.py': 'fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a'}

def private_traffic_source_libtorrent(path, manifest_path, build):
    """Read-only exact private preflight; no loader/subprocess/target invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unqualified private-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256)
    for name, pin in PRIVATE_TRAFFIC_RECORDS.items():
        pinned_file(manifest_path.parent / name, pin)
    manifest = json.loads(manifest_path.read_bytes())
    expected_roots = {key: str(Path(PRIVATE_TRAFFIC_CANDIDATE) / name) for key, name in (
        ("source_root", "source"), ("build_root", "build"), ("install_root", "install"))}
    if (any(manifest.get(key) != value for key, value in expected_roots.items())
            or manifest.get("source_build_record") != "provider-traffic-source-build-record.json"
            or manifest.get("source_build_record_sha256") != PRIVATE_TRAFFIC_RECORDS["provider-traffic-source-build-record.json"]
            or manifest.get("purpose") != "source-bound-qualification-only"
            or manifest.get("causal_qualified") is not False):
        raise ValueError("Wrong exact private-provider source/history cohort")
    source = path.absolute()
    pinned_file(source, PRIVATE_TRAFFIC_LIBTORRENT_SHA256)
    if (str(source) != manifest.get("artifact")
            or source != Path(expected_roots["install_root"]) / "lib/libtorrent-rasterbar.so.2.0.12"
            or manifest.get("artifact_sha256") != PRIVATE_TRAFFIC_LIBTORRENT_SHA256
            or type(manifest.get("artifact_bytes")) is not int
            or manifest["artifact_bytes"] != PRIVATE_TRAFFIC_LIBTORRENT_BYTES
            or source.stat().st_size != PRIVATE_TRAFFIC_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact privately qualified traffic-V3 source artifact")
    payload = source.read_bytes()
    if (len(payload) != PRIVATE_TRAFFIC_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_TRAFFIC_LIBTORRENT_SHA256):
        raise ValueError("Private-provider source image changed during preflight")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    # Both recursive helpers are pinned before executing the outer definitions.
    # No import by ambient module name, path/version fallback or marker trust.
    for name, pin in PRIVATE_TRAFFIC_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_private_next_qualification",
                                                binding / "verify_runtime_manifest_traffic.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(expected_roots["install_root"]) / "include")
    if pin != PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Private provider failed complete source/build/SDK/history admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt_path = generated / "configure-receipt.json"
    header_path = generated / "persistent_runtime_config.hpp"
    for candidate in (receipt_path, header_path):
        if candidate.is_symlink() or not candidate.is_file() or candidate.resolve(strict=True) != candidate:
            raise ValueError("Private-provider generated binding is not a canonical regular file")
    receipt = json.loads(receipt_path.read_bytes())
    expected = {"manifest_sha256": PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": PRIVATE_TRAFFIC_LIBTORRENT_SHA256, "artifact": str(source),
        "provider_queue_observer": True, "provider_traffic_v3": True,
        "causal_qualified": False, "purpose": "source-bound-qualification-only"}
    if (receipt != expected or any(type(receipt[key]) is not type(value) for key, value in expected.items())):
        raise ValueError("Application was not configured against this exact private provider/SDK")
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        "inline constexpr char artifact[] = " + json.dumps(str(source)) + ";\n"
        "inline constexpr char sha256[] = " + json.dumps(PRIVATE_TRAFFIC_LIBTORRENT_SHA256) + ";\n"
        "inline constexpr char manifest[] = " + json.dumps(PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256) + ";\n"
        "inline constexpr unsigned long long bytes = 9090088ULL;\n}\n")
    if header_path.read_text() != header:
        raise ValueError("Application compiled binding does not select this exact private provider")
    return source, payload


def install_private_traffic_libtorrent(destination, dependency, source, payload):
    """Only after exact private preflight, into main's fresh output directory."""
    if (len(payload) != PRIVATE_TRAFFIC_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_TRAFFIC_LIBTORRENT_SHA256):
        raise ValueError("Changed private-provider bytes before installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh private distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh private distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or alias.stat().st_size != PRIVATE_TRAFFIC_LIBTORRENT_BYTES
                    or hashlib.sha256(alias.read_bytes()).hexdigest() != PRIVATE_TRAFFIC_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias differs from the exact private provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-private-socks4a-webseed-queue-traffic-v3", "version": "2.0.12",
        "artifact_sha256": PRIVATE_TRAFFIC_LIBTORRENT_SHA256, "artifact_bytes": PRIVATE_TRAFFIC_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": PRIVATE_TRAFFIC_RECORDS["provider-traffic-source-build-record.json"],
        "parent_source_build_record_sha256": PRIVATE_TRAFFIC_RECORDS["provider-queue-source-build-record.json"],
        "runtime_manifest_sha256": PRIVATE_TRAFFIC_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False, "shipping_approved": False,
        "distribution_scope": "private-qualification-only",
        "scope": "Exact private source/SDK-qualified dependency; application runtime qualification is separate",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


# Additive exact traffic-V3 lane. Previous selectors/functions stay unchanged.
PRIVATE_SOCKS_LIBTORRENT_SHA256 = 'a5b78ccb4ee17a6773899e797d867844c8a24a45358e260b7ab65a93e2954187'
PRIVATE_SOCKS_LIBTORRENT_BYTES = 9139888
PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256 = '2a5e56dd4987756e74871c3aad015ad93974e6d47aad8754b0ad05d09f3ca94e'
PRIVATE_SOCKS_CANDIDATE = '/home/yurii/Java/BiglyBT/build/network-reliability-20261028/provider-socks-lifecycle'
PRIVATE_SOCKS_RECORDS = {'provider-socks-source-build-record.json': '9f8d1f436f943994a117f9f33bbaf016f92bb60ab3c0cecf9e0b6f02efb3c6ee', 'provider-traffic-source-build-record.json': '91218c17d3e74bb79a0fea0cfbbf3734e3422895e59f46fad4cb6b17e2927fd0', 'provider-queue-source-build-record.json': '598a2501d1fb3175df4d7fe49e46add045ba78e79c9c0288b7d5b04b4cbaf087', 'provider-webseed-source-build-record.json': 'a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08', 'provider-next-source-build-record.json': '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca', 'provider-source-build-record.json': '34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f', 'source-build-record.json': '436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66'}
PRIVATE_SOCKS_VERIFIERS = {'verify_runtime_manifest_socks.py': '3a6cc04acda2d6ded210fa81487f2bc3ccf50d2c8b13ad3a2ec5025f4632eded', 'verify_runtime_manifest_traffic.py': '8e37f7ad597ea4d8af5665c35db4466d6e08d367e96380c0c3118454c8b7834b', 'verify_runtime_manifest_queue.py': 'd53a3efa5160764ac48212c0ce0c85aa0fad4e1d1760ea13c3c9f2e2aa6a0bf0', 'verify_runtime_manifest_webseed.py': 'b07b80c7aa6b0e1371f583eb15051b337cb4d8e96af76c65da5e20bcf6f00ab0', 'verify_runtime_manifest.py': 'db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c', 'verify_provider_next_runtime.py': '0922c9960a507c0b453b240ba3ca5b56c595eaf256fbcf5ce4bcf2d968b88e32', 'verify_provider_runtime.py': 'fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a'}

def private_socks_source_libtorrent(path, manifest_path, build):
    """Read-only exact private preflight; no loader/subprocess/target invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unqualified private-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256)
    for name, pin in PRIVATE_SOCKS_RECORDS.items():
        pinned_file(manifest_path.parent / name, pin)
    manifest = json.loads(manifest_path.read_bytes())
    expected_roots = {key: str(Path(PRIVATE_SOCKS_CANDIDATE) / name) for key, name in (
        ("source_root", "source"), ("build_root", "build"), ("install_root", "install"))}
    if (any(manifest.get(key) != value for key, value in expected_roots.items())
            or manifest.get("source_build_record") != "provider-socks-source-build-record.json"
            or manifest.get("source_build_record_sha256") != PRIVATE_SOCKS_RECORDS["provider-socks-source-build-record.json"]
            or manifest.get("purpose") != "source-bound-qualification-only"
            or manifest.get("causal_qualified") is not False):
        raise ValueError("Wrong exact private-provider source/history cohort")
    source = path.absolute()
    pinned_file(source, PRIVATE_SOCKS_LIBTORRENT_SHA256)
    if (str(source) != manifest.get("artifact")
            or source != Path(expected_roots["install_root"]) / "lib/libtorrent-rasterbar.so.2.0.12"
            or manifest.get("artifact_sha256") != PRIVATE_SOCKS_LIBTORRENT_SHA256
            or type(manifest.get("artifact_bytes")) is not int
            or manifest["artifact_bytes"] != PRIVATE_SOCKS_LIBTORRENT_BYTES
            or source.stat().st_size != PRIVATE_SOCKS_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact privately qualified traffic-V3 source artifact")
    payload = source.read_bytes()
    if (len(payload) != PRIVATE_SOCKS_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_SOCKS_LIBTORRENT_SHA256):
        raise ValueError("Private-provider source image changed during preflight")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    # Both recursive helpers are pinned before executing the outer definitions.
    # No import by ambient module name, path/version fallback or marker trust.
    for name, pin in PRIVATE_SOCKS_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_private_next_qualification",
                                                binding / "verify_runtime_manifest_socks.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(expected_roots["install_root"]) / "include")
    if pin != PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Private provider failed complete source/build/SDK/history admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt_path = generated / "configure-receipt.json"
    header_path = generated / "persistent_runtime_config.hpp"
    for candidate in (receipt_path, header_path):
        if candidate.is_symlink() or not candidate.is_file() or candidate.resolve(strict=True) != candidate:
            raise ValueError("Private-provider generated binding is not a canonical regular file")
    receipt = json.loads(receipt_path.read_bytes())
    expected = {"manifest_sha256": PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": PRIVATE_SOCKS_LIBTORRENT_SHA256, "artifact": str(source),
        "provider_queue_observer": True, "provider_traffic_v3": True, "provider_socks_cancellation": True,
        "causal_qualified": False, "purpose": "source-bound-qualification-only"}
    if (receipt != expected or any(type(receipt[key]) is not type(value) for key, value in expected.items())):
        raise ValueError("Application was not configured against this exact private provider/SDK")
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        "inline constexpr char artifact[] = " + json.dumps(str(source)) + ";\n"
        "inline constexpr char sha256[] = " + json.dumps(PRIVATE_SOCKS_LIBTORRENT_SHA256) + ";\n"
        "inline constexpr char manifest[] = " + json.dumps(PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256) + ";\n"
        "inline constexpr unsigned long long bytes = 9139888ULL;\n}\n")
    if header_path.read_text() != header:
        raise ValueError("Application compiled binding does not select this exact private provider")
    return source, payload


def install_private_socks_libtorrent(destination, dependency, source, payload):
    """Only after exact private preflight, into main's fresh output directory."""
    if (len(payload) != PRIVATE_SOCKS_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_SOCKS_LIBTORRENT_SHA256):
        raise ValueError("Changed private-provider bytes before installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh private distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh private distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or alias.stat().st_size != PRIVATE_SOCKS_LIBTORRENT_BYTES
                    or hashlib.sha256(alias.read_bytes()).hexdigest() != PRIVATE_SOCKS_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias differs from the exact private provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-private-socks-cancellation-traffic-v3", "version": "2.0.12",
        "artifact_sha256": PRIVATE_SOCKS_LIBTORRENT_SHA256, "artifact_bytes": PRIVATE_SOCKS_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": PRIVATE_SOCKS_RECORDS["provider-socks-source-build-record.json"],
        "parent_source_build_record_sha256": PRIVATE_SOCKS_RECORDS["provider-traffic-source-build-record.json"],
        "runtime_manifest_sha256": PRIVATE_SOCKS_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False, "shipping_approved": False,
        "distribution_scope": "private-qualification-only",
        "scope": "Exact private source/SDK-qualified dependency; application runtime qualification is separate",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


# Additive exact root-reviewed private PORT qualification selector. Shipping remains separate.
PRIVATE_MAINLINE_LIBTORRENT_SHA256 = '9fbb550bbc1f877ce34ce21849deab97971f5d3eacfbc531a7b777d28a3b2eea'
PRIVATE_MAINLINE_LIBTORRENT_BYTES = 9153448
PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256 = '1267bf58a235dc6c32d9ffadab8bbd994ec81f2aa958f0cf621f8a71e4f6c84c'
PRIVATE_MAINLINE_CANDIDATE = '/home/yurii/Java/BiglyBT/build/mainline-port-20261028/provider-candidate'
PRIVATE_MAINLINE_RECORDS = dict(PRIVATE_SOCKS_RECORDS, **{'provider-mainline-source-build-record.json': 'c848411975cecc955370cf0a95790fd4659a52714b8f57ce9c82fa7e9fbf54d3'})
PRIVATE_MAINLINE_VERIFIERS = dict(PRIVATE_SOCKS_VERIFIERS, **{'verify_runtime_manifest_mainline.py': '59aff49764d663a5d8fe949903b675688e1e99d982bfd0a6a092c15a62a3dce7'})

def private_mainline_source_libtorrent(path, manifest_path, build):
    """Read-only exact private preflight; no loader/subprocess/target invocation."""
    import importlib.util
    import sys
    sys.dont_write_bytecode = True

    def pinned_file(candidate, pin):
        if (not candidate.is_absolute() or candidate.is_symlink() or not candidate.is_file()
                or candidate.resolve(strict=True) != candidate
                or hashlib.sha256(candidate.read_bytes()).hexdigest() != pin):
            raise ValueError("Changed/unqualified private-provider input: " + str(candidate))

    manifest_path = manifest_path.absolute()
    pinned_file(manifest_path, PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256)
    for name, pin in PRIVATE_MAINLINE_RECORDS.items():
        pinned_file(manifest_path.parent / name, pin)
    manifest = json.loads(manifest_path.read_bytes())
    expected_roots = {key: str(Path(PRIVATE_MAINLINE_CANDIDATE) / name) for key, name in (
        ("source_root", "source"), ("build_root", "build"), ("install_root", "install"))}
    if (any(manifest.get(key) != value for key, value in expected_roots.items())
            or manifest.get("source_build_record") != "provider-mainline-source-build-record.json"
            or manifest.get("source_build_record_sha256") != PRIVATE_MAINLINE_RECORDS["provider-mainline-source-build-record.json"]
            or manifest.get("purpose") != "source-bound-qualification-only"
            or manifest.get("causal_qualified") is not False):
        raise ValueError("Wrong exact private-provider source/history cohort")
    source = path.absolute()
    pinned_file(source, PRIVATE_MAINLINE_LIBTORRENT_SHA256)
    if (str(source) != manifest.get("artifact")
            or source != Path(expected_roots["install_root"]) / "lib/libtorrent-rasterbar.so.2.0.12"
            or manifest.get("artifact_sha256") != PRIVATE_MAINLINE_LIBTORRENT_SHA256
            or type(manifest.get("artifact_bytes")) is not int
            or manifest["artifact_bytes"] != PRIVATE_MAINLINE_LIBTORRENT_BYTES
            or source.stat().st_size != PRIVATE_MAINLINE_LIBTORRENT_BYTES):
        raise ValueError("Expected the exact privately qualified PORT source artifact")
    payload = source.read_bytes()
    if (len(payload) != PRIVATE_MAINLINE_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_MAINLINE_LIBTORRENT_SHA256):
        raise ValueError("Private-provider source image changed during preflight")
    binding = Path(__file__).resolve().parents[2] / "native/engine/runtime-binding"
    # Both recursive helpers are pinned before executing the outer definitions.
    # No import by ambient module name, path/version fallback or marker trust.
    for name, pin in PRIVATE_MAINLINE_VERIFIERS.items():
        pinned_file(binding / name, pin)
    spec = importlib.util.spec_from_file_location("biglybtc_portable_private_mainline_qualification",
                                                binding / "verify_runtime_manifest_mainline.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    admitted, pin = verifier.inspect(manifest_path, source, Path(expected_roots["install_root"]) / "include")
    if pin != PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256 or admitted != manifest:
        raise ValueError("Private provider failed complete source/build/SDK/history admission")
    generated = build.resolve(strict=True) / "engine/runtime-binding"
    receipt_path = generated / "configure-receipt.json"
    header_path = generated / "persistent_runtime_config.hpp"
    for candidate in (receipt_path, header_path):
        if candidate.is_symlink() or not candidate.is_file() or candidate.resolve(strict=True) != candidate:
            raise ValueError("Private-provider generated binding is not a canonical regular file")
    receipt = json.loads(receipt_path.read_bytes())
    expected = {"manifest_sha256": PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256,
        "artifact_sha256": PRIVATE_MAINLINE_LIBTORRENT_SHA256, "artifact": str(source),
        "provider_queue_observer": True, "provider_traffic_v3": True, "provider_socks_cancellation": True, "provider_mainline_port": True,
        "causal_qualified": False, "purpose": "source-bound-qualification-only"}
    if (receipt != expected or any(type(receipt[key]) is not type(value) for key, value in expected.items())):
        raise ValueError("Application was not configured against this exact private provider/SDK")
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        "inline constexpr char artifact[] = " + json.dumps(str(source)) + ";\n"
        "inline constexpr char sha256[] = " + json.dumps(PRIVATE_MAINLINE_LIBTORRENT_SHA256) + ";\n"
        "inline constexpr char manifest[] = " + json.dumps(PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256) + ";\n"
        "inline constexpr unsigned long long bytes = 9153448ULL;\n}\n")
    if header_path.read_text() != header:
        raise ValueError("Application compiled binding does not select this exact private provider")
    return source, payload


def install_private_mainline_libtorrent(destination, dependency, source, payload):
    """Only after exact private preflight, into main's fresh output directory."""
    if (len(payload) != PRIVATE_MAINLINE_LIBTORRENT_BYTES
            or hashlib.sha256(payload).hexdigest() != PRIVATE_MAINLINE_LIBTORRENT_SHA256):
        raise ValueError("Changed private-provider bytes before installation")
    private = destination / "usr/lib/biglybtc/lib"
    lookup = dependency / "usr/lib/x86_64-linux-gnu"
    for directory in (private, lookup):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.is_symlink() or not directory.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Runtime directory escapes the fresh private distribution: " + str(directory))
        for alias in directory.glob("libtorrent-rasterbar.so*"):
            if alias.name not in LIBTORRENT_NAMES or (alias.is_dir() and not alias.is_symlink()):
                raise ValueError("Unexpected runtime alias in the fresh private distribution: " + str(alias))
            alias.unlink()
    canonical = private / LIBTORRENT_NAMES[-1]
    canonical.write_bytes(payload)
    canonical.chmod(0o644)
    for name in LIBTORRENT_NAMES[:-1]:
        (private / name).symlink_to(canonical.name)
    for name in LIBTORRENT_NAMES:
        (lookup / name).symlink_to(os.path.relpath(canonical, lookup))
    aliases = []
    for directory in (private, lookup):
        for name in LIBTORRENT_NAMES:
            alias = directory / name
            if (alias.resolve(strict=True) != canonical or alias.stat().st_size != PRIVATE_MAINLINE_LIBTORRENT_BYTES
                    or hashlib.sha256(alias.read_bytes()).hexdigest() != PRIVATE_MAINLINE_LIBTORRENT_SHA256):
                raise ValueError("Runtime alias differs from the exact private provider: " + str(alias))
            aliases.append(alias.relative_to(destination).as_posix())
    return {"provider": "source-built-private-mainline-port-traffic-v3", "version": "2.0.12",
        "artifact_sha256": PRIVATE_MAINLINE_LIBTORRENT_SHA256, "artifact_bytes": PRIVATE_MAINLINE_LIBTORRENT_BYTES,
        "source_input": str(source), "upstream_commit": SOURCE_LIBTORRENT_COMMIT,
        "source_build_record_sha256": PRIVATE_MAINLINE_RECORDS["provider-mainline-source-build-record.json"],
        "parent_source_build_record_sha256": PRIVATE_MAINLINE_RECORDS["provider-socks-source-build-record.json"],
        "runtime_manifest_sha256": PRIVATE_MAINLINE_RUNTIME_MANIFEST_SHA256,
        "artifact": canonical.relative_to(destination).as_posix(), "aliases": aliases,
        "instrumented": False, "causal_qualified": False, "shipping_approved": False,
        "distribution_scope": "private-qualification-only",
        "scope": "Exact private source/SDK-qualified dependency; application runtime qualification is separate",
        "package_role": "Original Debian package metadata and notices retained; its shared objects replaced"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libtorrent-package", type=Path, required=True)
    parser.add_argument("--source-libtorrent", type=Path,
        help="Optionally bundle only the exact approved uninstrumented GCC16 libtorrent image")
    parser.add_argument("--source-provider-manifest", type=Path,
        help="Explicit additional exact selected-swarm/counter provider manifest; no instrumented or version fallback")
    parser.add_argument("--private-source-provider-manifest", type=Path,
        help="Private qualification only: exact b0e/3342 source/SDK cohort; never a shipping selector")
    parser.add_argument("--private-webseed-provider-manifest", type=Path,
        help="Private qualification only: exact d33c source/SDK/13-export cohort; no shipping selector")
    parser.add_argument("--private-traffic-provider-manifest", type=Path,
        help="Private qualification only: exact traffic-V3 source/SDK/history cohort; no shipping selector")
    parser.add_argument("--private-socks-provider-manifest", type=Path,
        help="Private qualification only: exact cancellation-fixed source/SDK/history cohort")
    parser.add_argument("--private-mainline-provider-manifest", type=Path,
        help="Private qualification only: exact PORT source/SDK/history cohort")
    args = parser.parse_args()
    if args.private_mainline_provider_manifest is not None:
        if any(value is not None for value in (args.private_socks_provider_manifest,
                args.private_traffic_provider_manifest, args.private_webseed_provider_manifest,
                args.private_source_provider_manifest, args.source_provider_manifest)):
            parser.error("Select one explicit private or existing provider manifest")
        if args.source_libtorrent is None:
            parser.error("--private-mainline-provider-manifest also requires --source-libtorrent")
    if args.private_socks_provider_manifest is not None:
        if any(value is not None for value in (args.private_traffic_provider_manifest, args.private_webseed_provider_manifest, args.private_source_provider_manifest, args.source_provider_manifest)):
            parser.error("Select one explicit private or existing provider manifest")
        if args.source_libtorrent is None:
            parser.error("--private-socks-provider-manifest also requires --source-libtorrent")
    if args.private_traffic_provider_manifest is not None:
        if any(value is not None for value in (args.private_webseed_provider_manifest,
                args.private_source_provider_manifest, args.source_provider_manifest)):
            parser.error("Select one explicit private or existing provider manifest")
        if args.source_libtorrent is None:
            parser.error("--private-traffic-provider-manifest also requires --source-libtorrent")
    if args.private_webseed_provider_manifest is not None:
        if args.private_source_provider_manifest is not None or args.source_provider_manifest is not None:
            parser.error("Select one explicit private or existing provider manifest")
        if args.source_libtorrent is None:
            parser.error("--private-webseed-provider-manifest also requires --source-libtorrent")
    if args.private_source_provider_manifest is not None:
        if args.source_provider_manifest is not None:
            parser.error("Select either the existing provider manifest or the explicit private qualification manifest")
        if args.source_libtorrent is None:
            parser.error("--private-source-provider-manifest also requires --source-libtorrent")
    if args.source_provider_manifest is not None and args.source_libtorrent is None:
        parser.error("--source-provider-manifest also requires --source-libtorrent")
    source = None
    if args.source_libtorrent is not None:
        try:
            if args.private_mainline_provider_manifest is not None:
                source = private_mainline_source_libtorrent(args.source_libtorrent, args.private_mainline_provider_manifest, args.build)
            elif args.private_socks_provider_manifest is not None:
                source = private_socks_source_libtorrent(args.source_libtorrent, args.private_socks_provider_manifest, args.build)
            elif args.private_traffic_provider_manifest is not None:
                source = private_traffic_source_libtorrent(args.source_libtorrent,
                    args.private_traffic_provider_manifest, args.build)
            elif args.private_webseed_provider_manifest is not None:
                source = private_webseed_source_libtorrent(args.source_libtorrent,
                    args.private_webseed_provider_manifest, args.build)
            elif args.private_source_provider_manifest is not None:
                source = private_next_source_libtorrent(args.source_libtorrent,
                    args.private_source_provider_manifest, args.build)
            else:
                source = (source_libtorrent(args.source_libtorrent) if args.source_provider_manifest is None
                          else selected_source_libtorrent(args.source_libtorrent, args.source_provider_manifest, args.build))
        except (OSError, ValueError) as error:
            parser.error(str(error))
    destination = args.output.absolute()
    package = args.libtorrent_package.resolve()
    if destination.exists() or destination.is_symlink():
        parser.error("Use a fresh distribution directory; existing builds are preserved")
    if source is not None:
        destination = destination.resolve()
    identity = subprocess.check_output(["dpkg-deb", "--field", str(package),
        "Package", "Version", "Architecture"], text=True)
    if identity != "Package: libtorrent-rasterbar2.0t64\nVersion: 2.0.12-1\nArchitecture: amd64\n":
        parser.error("Expected the Ubuntu amd64 libtorrent 2.0.12 runtime package")
    destination.mkdir(parents=True)
    environment = os.environ.copy()
    environment["DESTDIR"] = str(destination)
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", "/usr", "--strip"],
                   env=environment, check=True, timeout=180)
    dependency = destination / "dependencies/libtorrent"
    dependency.parent.mkdir()
    subprocess.run(["dpkg-deb", "--extract", str(package), str(dependency)], check=True, timeout=60)
    provider = None
    if source is not None:
        if args.private_mainline_provider_manifest is not None:
            provider = install_private_mainline_libtorrent(destination, dependency, *source)
        elif args.private_socks_provider_manifest is not None:
            provider = install_private_socks_libtorrent(destination, dependency, *source)
        elif args.private_traffic_provider_manifest is not None:
            provider = install_private_traffic_libtorrent(destination, dependency, *source)
        elif args.private_webseed_provider_manifest is not None:
            provider = install_private_webseed_libtorrent(destination, dependency, *source)
        elif args.private_source_provider_manifest is not None:
            provider = install_private_next_libtorrent(destination, dependency, *source)
        else:
            provider = (install_source_libtorrent(destination, dependency, *source) if args.source_provider_manifest is None
                        else install_selected_libtorrent(destination, dependency, *source))
    lookup = "dependencies/libtorrent/usr/lib/x86_64-linux-gnu"
    library_path = "str(root / " + repr(lookup) + ")"
    if provider is not None:
        library_path = "str(root / 'usr/lib/biglybtc/lib') + os.pathsep + " + library_path
    launcher = destination / "BiglyBTC-preview"
    launcher.write_text("#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\n"
        "root = Path(__file__).resolve().parent\n"
        f"os.environ['LD_LIBRARY_PATH'] = {library_path}\n"
        "os.execv(str(root / 'usr/bin/BiglyBTC-preview'), ['BiglyBTC-preview', *sys.argv[1:]])\n")
    launcher.chmod(0o755)
    # Normal product entry point is relocatable too. No profile, listen port or
    # service policy is selected here; main requires explicit --profile input.
    normal_launcher = destination / "BiglyBTC"
    normal_launcher.write_text(
        '#!/usr/bin/env python3\n'
        'import os, sys\n'
        'from pathlib import Path\n'
        'root = Path(__file__).resolve().parent\n'
        "private = root / 'usr/lib/biglybtc'\n"
        "lookup = root / 'dependencies/libtorrent/usr/lib/x86_64-linux-gnu'\n"
        "os.environ['LD_LIBRARY_PATH'] = os.pathsep.join(map(str, [private / 'lib', private / 'qt/lib', lookup]))\n"
        "os.environ['QT_PLUGIN_PATH'] = str(private / 'qt/plugins')\n"
        "os.environ['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(private / 'qt/plugins/platforms')\n"
        "os.execv(str(private / 'bin/BiglyBTC'), ['BiglyBTC', *sys.argv[1:]])\n"
    )
    normal_launcher.chmod(0o755)
    (destination / "README.txt").write_text(
        "BiglyBTC portable development client\n\n"
        "Normal client: ./BiglyBTC --profile /absolute/path/to/profile\n"
        "Pass an existing absolute profile directory without symbolic links.\n"
        "For development, use a separate owned directory such as ~/.biglybtc-dev;\n"
        "expand it to its absolute path when passing --profile. The launcher\n"
        "does not create a profile, choose ports or copy the Java profile.\n"
        "The original ~/.biglybt directory is never chosen automatically.\n\n"
        "Offline generated-sample preview:\n"
        "Run ./BiglyBTC-preview from this folder. Qt and the libtorrent runtime\n"
        "are included alongside the application; no system installation is needed.\n"
        "The four torrents are generated examples. Transfers are disabled.\n"
        "Check My Torrents/Ctrl+1, My Tracker/Ctrl+2, My Shares/Ctrl+3, sorting and Files/Ctrl+I.\n"
        "Saved service views are read-only; no tracker or share service is started.\n"
        "Also check Refresh/F5, scaling, auxiliary tab closure and window close.\n"
        "Each launch uses a fresh /tmp/biglybtc-preview-* sample profile.\n"
        "The original ~/.biglybt profile is never opened by this launcher.\n"
        "Full client/plugin migration and original-profile switching remain unfinished.\n")
    if provider is not None:
        with (destination / "README.txt").open("a") as notice:
            notice.write("\nThis folder uses the exact uninstrumented source-built libtorrent 2.0.12.\n"
                         "The extracted Debian package retains its metadata and license notices;\n"
                         "its shared objects resolve to the private source-built image.\n")
    if args.private_source_provider_manifest is not None or args.private_webseed_provider_manifest is not None or args.private_traffic_provider_manifest is not None or args.private_socks_provider_manifest is not None or args.private_mainline_provider_manifest is not None:
        with (destination / "README.txt").open("a") as notice:
            notice.write("Private qualification folder only; no shipping approval is implied.\n")
    environment["LD_LIBRARY_PATH"] = str(dependency / "usr/lib/x86_64-linux-gnu")
    if provider is not None:
        environment["LD_LIBRARY_PATH"] = (str(destination / "usr/lib/biglybtc/lib") +
                                           os.pathsep + environment["LD_LIBRARY_PATH"])
    linked = subprocess.check_output(["ldd", str(destination / "usr/lib/biglybtc/bin/BiglyBTC")],
                                     env=environment, text=True, timeout=30)
    if "not found" in linked or "/opt/Qt" in linked or "/tmp/" in linked:
        raise ValueError("Distribution retains a missing or build-only dependency: " + linked)
    loaders = {}
    if provider is not None:
        for name in ("BiglyBTC", "BiglyBTC-headless"):
            output = linked if name == "BiglyBTC" else subprocess.check_output(
                ["ldd", str(destination / "usr/lib/biglybtc/bin" / name)],
                env=environment, text=True, timeout=30)
            if "not found" in output or "/opt/Qt" in output or "/tmp/" in output:
                raise ValueError("Distribution retains a missing or build-only dependency: " + output)
            matches = re.findall(r"^\s*libtorrent-rasterbar\.so\.2\.0 => (.+?) \(", output, re.MULTILINE)
            if (len(matches) != 1 or Path(matches[0]).resolve(strict=True) !=
                    destination / provider["artifact"]):
                raise ValueError("Installed executable does not resolve the private source runtime: " + output)
            roots = (destination, Path("/usr/lib").resolve(),
                     Path("/lib").resolve(), Path("/lib64").resolve())
            for loaded in re.findall(r"=> (/.*?) \(", output):
                resolved = Path(loaded).resolve(strict=True)
                if not any(resolved.is_relative_to(root) for root in roots):
                    raise ValueError("Non-system dependency outside the distribution: " + loaded)
            loaders[name] = output
    country_bundle = None
    module = destination / "usr/lib/biglybtc/lib/BiglyBTC-plugin-azlocprov.so"
    approval = args.build / "app/generated/country_approval.hpp"
    if module.exists() or approval.exists():
        compiled = approval.read_text()
        approved_sha = re.search(r'Sha256\[\] = "([0-9a-f]{64})";', compiled)
        approved_bytes = re.search(r'Bytes = ([0-9]+);', compiled)
        version = re.search(r'Version\[\] = "([0-9.]+)";', compiled)
        if not approved_sha or not approved_bytes or not version:
            raise ValueError("Invalid compiled Country module approval")
        payload = module.read_bytes()
        if (len(payload) != int(approved_bytes[1]) or
                hashlib.sha256(payload).hexdigest() != approved_sha[1]):
            raise ValueError("Packaged Country module differs from its compiled approval")
        output = subprocess.check_output(["ldd", str(module)], env=environment, text=True, timeout=30)
        if "not found" in output or "/tmp/" in output or "/opt/Qt" in output:
            raise ValueError("Country module retains a missing or build-only dependency: " + output)
        for soname in ("libmaxminddb.so.0", "libarchive.so.13"):
            resolved = re.findall(r"^\s*" + re.escape(soname) + r" => (.+?) \(", output, re.MULTILINE)
            if len(resolved) != 1 or Path(resolved[0]).resolve() != module.parent / soname:
                raise ValueError("Country module does not use its private dependency: " + soname)
        country_bundle = {"artifact": module.relative_to(destination).as_posix(),
            "sha256": approved_sha[1], "bytes": len(payload), "version": version[1],
            "approval": "compiled application-owned bundle", "database_included": False,
            "loader": output}
    files = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            files[path.relative_to(destination).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        "scope": "Portable development client; explicit profile or generated offline preview; full migration remains unfinished",
        "build": str(args.build.resolve()), "launch": "./BiglyBTC-preview",
        "normal_launch": "./BiglyBTC --profile ABSOLUTE_PROFILE",
        "dependency_package_sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
        "files_sha256": files, "loader": linked, "system_installation": False,
    }
    if args.private_source_provider_manifest is not None or args.private_webseed_provider_manifest is not None or args.private_traffic_provider_manifest is not None or args.private_socks_provider_manifest is not None or args.private_mainline_provider_manifest is not None:
        manifest["distribution_scope"] = "private-qualification-only"
    if country_bundle is not None:
        manifest["country_bundle"] = country_bundle
    if provider is not None:
        manifest["libtorrent_runtime"] = provider
        manifest["loaders"] = loaders
    (destination / "BUILD_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Runnable preview:", launcher)


if __name__ == "__main__":
    main()
