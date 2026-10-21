"""Exact reviewed test-only source/image binding. No provider fallback or trust flag."""
import json
from pathlib import Path

MANIFEST_SHA = '37ce74d783fe42e42f09415ffd4358f330d5849803ef1064cb5abab7f89c5abc'
RECORD_SHA = 'c2a18c7d10684fd5b943e5e2d167e57d7417b3d1fddc0d641904386d41e47b91'
OVERLAY_SHA = "e9233edf660ff2ce461277b9a1dbd5eb3377c5dc52f2a8ef05a20557dfbbfd36"
PARENT_SHA = "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66"


def inspect(path, imported_library, imported_include, common):
    fail, digest, regular = common.fail, common.digest, common.regular
    if digest(regular(path)) != MANIFEST_SHA:
        fail("Instrumented manifest is outside the exact catalog")
    manifest = json.loads(path.read_bytes())
    if (set(manifest) != common.MANIFEST_KEYS or type(manifest["schema"]) is not int
            or manifest["schema"] != 1 or manifest["purpose"] != "source-bound-qualification-only"
            or manifest["causal_qualified"] is not False
            or manifest["source_build_record"] != "instrumented-source-build-record-v2.json"
            or manifest["source_build_record_sha256"] != RECORD_SHA):
        fail("Invalid qualification-only instrumented manifest")
    record_path = regular(path.parent / "instrumented-source-build-record-v2.json")
    overlay_path = regular(path.parent / "causal-overlay-manifest-v2.json")
    parent_path = regular(path.parent / "source-build-record.json")
    if digest(record_path) != RECORD_SHA or digest(overlay_path) != OVERLAY_SHA or digest(parent_path) != PARENT_SHA:
        fail("Changed complete instrumented/overlay/parent record")
    record, overlay, parent = [json.loads(p.read_bytes()) for p in (record_path, overlay_path, parent_path)]
    if (record["parent_record_sha256"] != PARENT_SHA or record["overlay_manifest_sha256"] != OVERLAY_SHA
            or record["overlay_patch_sha256"] != overlay["artifacts_sha256"]["libtorrent-causal.patch"]
            or record["causal_qualified"] is not False
            or record["status"] != "ISOLATED_INSTRUMENTED_IMAGE_BUILT_NOT_CATALOG_APPROVED_OR_CAUSALLY_QUALIFIED"):
        fail("Instrumented record has wrong parent/overlay/status")
    roots = {key: common.canonical_directory(manifest[key]) for key in ("source_root", "build_root", "install_root")}
    if any(record[key] != str(value) for key, value in roots.items()):
        fail("Instrumented source/build/install roots changed")
    source, build, install = [roots[key] for key in ("source_root", "build_root", "install_root")]
    expected_source = dict(parent["source_sha256"])
    for name, before in overlay["before_sha256"].items():
        if expected_source.get(name) != before:
            fail("Overlay beforeimage does not match parent source")
    expected_source.update(overlay["source_sha256"])
    if record["source_sha256"] != expected_source or record["bundled_dependency_sha256"] != parent["bundled_dependency_sha256"]:
        fail("Unexpected compiled source delta")
    rules = dict(parent["source_build_rules_sha256"])
    rules["CMakeLists.txt"] = record["source_build_rules_sha256"]["CMakeLists.txt"]
    rules["biglybtc-causal-target.cmake"] = record["source_build_rules_sha256"]["biglybtc-causal-target.cmake"]
    if record["source_build_rules_sha256"] != rules:
        fail("Unexpected upstream build-rule delta")
    common.verify_map(record, "source_sha256", source, 446)
    common.verify_map(record, "source_build_rules_sha256", source, 14)
    common.verify_map(record, "bundled_dependency_sha256", source, 16)
    common.verify_map(record, "installed_sha256", install, 283)
    common.verify_map(record, "build_files_sha256", build, 5)
    # Bind every copied source-tree leaf, including the actual appended rules.
    complete = record["source_inputs_sha256"]
    common.verify_map(record, "source_inputs_sha256", source, len(complete))
    if {str(p.relative_to(source)) for p in source.rglob("*") if p.is_file()} != set(complete):
        fail("Unrecorded instrumented source-tree leaf")
    artifact = regular(Path(manifest["artifact"]))
    if (artifact != artifact.resolve(strict=True) or not artifact.is_relative_to(install)
            or str(artifact) != record["artifact"] or manifest["artifact_sha256"] != record["artifact_sha256"]
            or type(manifest["artifact_bytes"]) is not int or manifest["artifact_bytes"] <= 0
            or artifact.stat().st_size != manifest["artifact_bytes"]
            or manifest["artifact_bytes"] != record["artifact_bytes"] or digest(artifact) != manifest["artifact_sha256"]):
        fail("Changed instrumented runtime image")
    if imported_library.resolve(strict=True) != artifact or imported_include.resolve(strict=True) != install / "include":
        fail("Imported target selects a different instrumented image/headers")
    return manifest, MANIFEST_SHA
