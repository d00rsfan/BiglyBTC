#!/usr/bin/env python3
"""Configure-time source/artifact binding. No loader or target execution."""
import argparse
import hashlib
import json
from pathlib import Path
import stat

# This is a reviewed source catalog, not a CMake/env caller override. Instrumented
# artifacts require a new reviewed manifest AND an explicit catalog amendment.
APPROVED = {"010c6094b62e5e2e597ce6267b0125f6560d1a043a792218389ef4b962e871ae":
            "source-bound-qualification-only",
            "aa5f1767640fc1ca5f978360ddb8eebd26fc29b2f0c34b44fdf7416299a1bb3c":
            "source-bound-qualification-only"}
RETENTION_SHA = "9b9ba22c14eafac097aecea13097ef59be6b686ff7150814b5d8889f6acc3b91"
RECORD_SHA = "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66"
UPSTREAM = "740a0b9aeabe00e762cc0efe4a0f27593db2550b"
# Replaced with ONE exact actual manifest SHA only after a separately reviewed build.
PROVIDER_MANIFEST_SHA = "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737"
NEXT_PROVIDER_MANIFEST_SHA = "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"
MANIFEST_KEYS = {"schema", "purpose", "causal_qualified", "source_build_record",
                 "source_build_record_sha256", "source_root", "build_root",
                 "install_root", "artifact", "artifact_sha256", "artifact_bytes"}


def fail(message):
    raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def regular(path):
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        fail(f"Not a regular input: {path}")
    return path


def canonical_directory(value):
    if not isinstance(value, str):
        fail("Directory must be text")
    path = Path(value)
    if not path.is_absolute() or path != path.resolve(strict=True) or not path.is_dir():
        fail(f"Noncanonical directory: {path}")
    return path


def leaf(root, relative):
    if not isinstance(relative, str):
        fail("Map key must be text")
    rel = Path(relative)
    if rel.is_absolute() or not rel.parts or any(x in (".", "..") for x in rel.parts):
        fail("Unsafe map key")
    # Installed LT SONAME links are allowed only within their recorded root.
    path = (root / rel).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        fail("Map leaf escapes its root or is not regular")
    return path


def verify_map(record, key, root, count):
    values = record.get(key)
    if not isinstance(values, dict) or len(values) != count:
        fail(f"Wrong {key} count")
    for relative, expected in values.items():
        if not isinstance(expected, str) or len(expected) != 64 or digest(leaf(root, relative)) != expected:
            fail(f"Changed {key} leaf: {relative}")


def inspect(manifest_path, imported_library, imported_include):
    manifest_path = regular(manifest_path)
    manifest_sha = digest(manifest_path)
    # Separate exact next-provider proposal. Placeholder cannot admit a digest.
    if manifest_sha == NEXT_PROVIDER_MANIFEST_SHA:
        import importlib.util
        import sys
        from types import SimpleNamespace
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("biglybtc_next_provider_runtime",
            Path(__file__).with_name("verify_provider_next_runtime.py"))
        provider = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(provider)
        common = SimpleNamespace(fail=fail, digest=digest, regular=regular,
            canonical_directory=canonical_directory, verify_map=verify_map, MANIFEST_KEYS=MANIFEST_KEYS)
        return provider.inspect(manifest_path, imported_library, imported_include, common)
    # Additional exact provider lane. This template cannot admit any candidate.
    if manifest_sha == PROVIDER_MANIFEST_SHA:
        import importlib.util
        import sys
        from types import SimpleNamespace
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("biglybtc_provider_runtime",
            Path(__file__).with_name("verify_provider_runtime.py"))
        provider = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(provider)
        common = SimpleNamespace(fail=fail, digest=digest, regular=regular,
            canonical_directory=canonical_directory, verify_map=verify_map, MANIFEST_KEYS=MANIFEST_KEYS)
        return provider.inspect(manifest_path, imported_library, imported_include, common)
    # Separate exact test-only catalog: no original-source or version fallback.
    if manifest_sha == '37ce74d783fe42e42f09415ffd4358f330d5849803ef1064cb5abab7f89c5abc':
        import importlib.util
        import sys
        from types import SimpleNamespace
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("biglybtc_instrumented_runtime_v2",
            Path(__file__).with_name("verify_instrumented_runtime_v2.py"))
        instrumented = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(instrumented)
        common = SimpleNamespace(fail=fail, digest=digest, regular=regular,
            canonical_directory=canonical_directory, verify_map=verify_map, MANIFEST_KEYS=MANIFEST_KEYS)
        return instrumented.inspect(manifest_path, imported_library, imported_include, common)
    # Separate exact test-only catalog: no original-source or version fallback.
    if manifest_sha == '28374ad5900f04c533a13afe90e81a7332f8a62b52db30adb0ed4dfe2fbce8ee':
        import importlib.util
        import sys
        from types import SimpleNamespace
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location("biglybtc_instrumented_runtime",
            Path(__file__).with_name("verify_instrumented_runtime.py"))
        instrumented = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(instrumented)
        common = SimpleNamespace(fail=fail, digest=digest, regular=regular,
            canonical_directory=canonical_directory, verify_map=verify_map, MANIFEST_KEYS=MANIFEST_KEYS)
        return instrumented.inspect(manifest_path, imported_library, imported_include, common)
    if manifest_sha not in APPROVED:
        fail("Dependency manifest is not in the reviewed catalog")
    manifest = json.loads(manifest_path.read_bytes())
    if not isinstance(manifest, dict) or set(manifest) not in (
            MANIFEST_KEYS, MANIFEST_KEYS | {"retention_receipt", "retention_receipt_sha256"}):
        fail("Unknown/missing manifest field")
    if type(manifest["schema"]) is not int or manifest["schema"] != 1:
        fail("Wrong manifest schema")
    if manifest["purpose"] != APPROVED[manifest_sha] or manifest["causal_qualified"] is not False:
        fail("This catalog permits qualification-only candidates")
    if manifest["source_build_record"] != "source-build-record.json" or manifest["source_build_record_sha256"] != RECORD_SHA:
        fail("Wrong source-build record")
    record_path = regular(manifest_path.parent / manifest["source_build_record"])
    if digest(record_path) != RECORD_SHA:
        fail("Changed source-build record")
    record = json.loads(record_path.read_bytes())
    if record.get("upstream_commit") != UPSTREAM or record.get("status") != "source_build_complete_runtime_pending":
        fail("Wrong recorded source cohort")
    source = canonical_directory(manifest["source_root"])
    build = canonical_directory(manifest["build_root"])
    install = canonical_directory(manifest["install_root"])
    cache = record["cache"]
    if "retention_receipt" in manifest:
        if manifest["retention_receipt"] != "retained-runtime.json" or manifest["retention_receipt_sha256"] != RETENTION_SHA:
            fail("Wrong byte-retention receipt")
        retained_path = regular(manifest_path.parent / manifest["retention_receipt"])
        if digest(retained_path) != RETENTION_SHA:
            fail("Changed byte-retention receipt")
        retained = json.loads(retained_path.read_bytes())
        if (retained.get("status") != "BYTE_EXACT_RETAINED_CANDIDATE_NOT_CAUSALLY_QUALIFIED"
                or retained.get("rebuilt") is not False or retained.get("original_files_changed") is not False
                or retained.get("source_build_record_sha256") != RECORD_SHA
                or retained.get("original_roots") != {"source": cache["CMAKE_HOME_DIRECTORY"],
                    "build-provenance": cache["libtorrent_BINARY_DIR"], "install": cache["CMAKE_INSTALL_PREFIX"]}
                or retained.get("retained_roots") != {"source": str(source), "build-provenance": str(build), "install": str(install)}
                or retained.get("artifact") != manifest["artifact"]
                or retained.get("artifact_sha256") != manifest["artifact_sha256"]):
            fail("Byte-retention receipt does not bind these original/retained roots")
    elif (str(source) != cache["CMAKE_HOME_DIRECTORY"] or str(build) != cache["libtorrent_BINARY_DIR"]
            or str(install) != cache["CMAKE_INSTALL_PREFIX"]):
        fail("Source/build/install roots disagree with source-build record")
    verify_map(record, "source_sha256", source, 444)
    verify_map(record, "source_build_rules_sha256", source, 13)
    verify_map(record, "bundled_dependency_sha256", source, 16)
    verify_map(record, "installed_sha256", install, 282)
    verify_map(record, "build_files_sha256", build, 4)
    artifact = regular(Path(manifest["artifact"]))
    if artifact != artifact.resolve(strict=True) or not artifact.is_relative_to(install):
        fail("Noncanonical artifact")
    original_relative = Path(record["library"]).relative_to(cache["CMAKE_INSTALL_PREFIX"])
    if artifact != install / original_relative or manifest["artifact_sha256"] != record["library_sha256"]:
        fail("Artifact disagrees with source-build record")
    if type(manifest["artifact_bytes"]) is not int or manifest["artifact_bytes"] <= 0:
        fail("Invalid artifact size")
    if artifact.stat().st_size != manifest["artifact_bytes"] or digest(artifact) != manifest["artifact_sha256"]:
        fail("Changed runtime artifact")
    if imported_library.resolve(strict=True) != artifact:
        fail("Imported LT target selects another artifact")
    if imported_include.resolve(strict=True) != install / "include":
        fail("Imported LT headers select another install")
    return manifest, manifest_sha


def cpp(value):
    # JSON escaping is valid for these ASCII absolute path/digest constants.
    if not value.isascii() or any(ord(c) < 32 for c in value):
        fail("Generated constant is not printable ASCII")
    return json.dumps(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--include", required=True, dest="include_root", type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    manifest, manifest_sha = inspect(args.manifest, args.library, args.include_root)
    header = ("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
              "namespace biglybtc::engine::detail::runtime_binding {\n"
              f"inline constexpr char artifact[] = {cpp(manifest['artifact'])};\n"
              f"inline constexpr char sha256[] = {cpp(manifest['artifact_sha256'])};\n"
              f"inline constexpr char manifest[] = {cpp(manifest_sha)};\n"
              f"inline constexpr unsigned long long bytes = {manifest['artifact_bytes']}ULL;\n"
              "}\n")
    # All validation precedes generated outputs. CMake stops on any exception.
    args.header.write_text(header)
    if manifest_sha == NEXT_PROVIDER_MANIFEST_SHA:
        args.receipt.write_text(json.dumps({"manifest_sha256": manifest_sha,
            "artifact_sha256": manifest["artifact_sha256"], "artifact": manifest["artifact"],
            "source_files": 451, "full_source_files": 1074, "build_rules": 13,
            "bundled_inputs": 16, "installed_files": 285, "sdk_headers": 276, "build_files": 4,
            "causal_qualified": False, "purpose": "source-bound-qualification-only",
            "provider_counter_swarm_socks_candidate": True}, indent=2) + "\n")
        return
    if manifest_sha == PROVIDER_MANIFEST_SHA:
        args.receipt.write_text(json.dumps({"manifest_sha256": manifest_sha,
            "artifact_sha256": manifest["artifact_sha256"], "artifact": manifest["artifact"],
            "source_files": 449, "full_source_files": 1072, "build_rules": 13,
            "bundled_inputs": 16, "installed_files": 284, "build_files": 4,
            "causal_qualified": False, "purpose": "source-bound-qualification-only",
            "provider_counter_and_swarm_candidate": True}, indent=2) + "\n")
        return
    args.receipt.write_text(json.dumps({"manifest_sha256": manifest_sha,
        "artifact_sha256": manifest["artifact_sha256"], "artifact": manifest["artifact"],
        "source_files": 446 if manifest["source_build_record"].startswith("instrumented-") else 444,
        "build_rules": 14 if manifest["source_build_record"].startswith("instrumented-") else 13,
        "installed_files": 283 if manifest["source_build_record"].startswith("instrumented-") else 282,
        "build_files": 5 if manifest["source_build_record"].startswith("instrumented-") else 4,
        "causal_qualified": False,
        "purpose": "source-bound-qualification-only"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
