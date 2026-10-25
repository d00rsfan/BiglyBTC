"""One exact source-built provider candidate; no target/loader execution."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat

# Both are deliberately non-digests until the actual build is recorded and a
# root-reviewed staged amendment substitutes ONE pair of exact SHA256 values.
MANIFEST_SHA = "e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737"
RECORD_SHA = "34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f"
PARENT_SHA = "436931d4c6b87274c60b89877afedc19e06485bc4f04d120a3ab3e1889abfc66"
SOURCE_MAP_SHA = "3a9f418dae16a53196d37e0f1556893e7b45425741019d60f1f0aa069be22826"
FULL_SOURCE_MAP_SHA = "3395e38322a6377778607ae24ab8a46b2dce483ca85ed8525f05f73d03206e3b"
RULES_MAP_SHA = "33382d248a5704ded57ac4888a6ec619eadd895538add123a47e58d5bb8a782b"
BUNDLED_MAP_SHA = "df8884e03b9fe1572db421d222cacb167f8be25b7be162d7bda9711a3d0827a0"
INSTALL_KEYS_SHA = "ac52f01093fff545b53aa621d160540c01b5388a1ed36eff4823dd285d4b7f93"
BUILD_KEYS_SHA = "286ffe037b351047d9c9c2e2c29bafdbe301c4a17f4c6718c6e6f0ab8149d270"
OVERLAYS = {
    "provider/manifest.json": "72865b634db7f05e53f747daf14bf2bcc195487d3e9124fe80380427204561e9",
    "provider-oracle/handshake-amendment/manifest.json": "410be6d14469f7ebdaeec51f72562fc337a1c0a8f41ffadde6c022642c5e5278",
    "provider-counter/manifest.json": "f10386b92e233ee02038f295419f2600d8f1e212201b5711d7ac11e2eca27770",
    "ordering-accounting/manifest.json": "1885dd0c620aeb1141f56b98ed820dd0312534c8d6b882220bcf6fb66b6db16d",
    "ordering-accounting/amendments/canonical-test-order/manifest.json": "35e56d8c2d20ebf1e0f2f342f65bc7510886d9e908f7b9c7977433bd961cfe5a",
}
EXPORTS = [
    "_ZN10libtorrent7versionEv",
    "_ZN10libtorrent21session_stats_metricsEv",
    "_ZN10libtorrent14session_handle18post_session_statsEv",
    "_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE",
    "_ZNK10libtorrent19session_stats_alert8countersEv",
    "_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE",
    "biglybtc_swarm_mask_abi_v1",
    "biglybtc_payload_counter_abi_v1",
    "biglybtc_read_payload_counters_v1",
]
RECORD_KEYS = {
    "schema", "status", "causal_qualified", "upstream_commit", "parent_record_sha256",
    "overlay_manifests_sha256", "source_root", "build_root", "install_root",
    "source_inputs_sha256", "source_sha256", "source_build_rules_sha256",
    "bundled_dependency_sha256", "installed_sha256", "build_files_sha256", "cache",
    "artifact", "artifact_sha256", "artifact_bytes", "build_receipt", "build_receipt_sha256",
    "build_source_map", "build_source_map_sha256", "logs_sha256", "required_exports",
    "elf_soname", "export_scan_sha256", "dynamic_section_sha256", "recipe_sha256",
}


def map_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def tree_keys(root, allow_library_links=False):
    result = set()
    aliases = {"lib/libtorrent-rasterbar.so", "lib/libtorrent-rasterbar.so.2.0"}
    for directory, subdirectories, files in os.walk(root, followlinks=False):
        for name in subdirectories:
            if (Path(directory) / name).is_symlink():
                raise ValueError("Directory symlink in bound tree")
        for name in files:
            path = Path(directory) / name
            relative = str(path.relative_to(root))
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                target = (root / "lib/libtorrent-rasterbar.so.2.0.12").resolve(strict=True)
                if not allow_library_links or relative not in aliases or path.resolve(strict=True) != target:
                    raise ValueError("Unapproved symlink in bound tree")
            elif not stat.S_ISREG(mode):
                raise ValueError("Special file in bound tree")
            result.add(relative)
    return result


def cache_values(path):
    result = {}
    for line in path.read_text().splitlines():
        if not line or line.startswith(("//", "#")):
            continue
        key_type, separator, value = line.partition("=")
        key, colon, _ = key_type.partition(":")
        if not separator or not colon or key in result:
            raise ValueError("Malformed or duplicate CMake cache entry")
        result[key] = value
    return result


def exported_functions(text):
    found = {name: 0 for name in EXPORTS}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(":"):
            continue
        name = fields[7].split("@", 1)[0]
        if name in found and fields[3] == "FUNC" and fields[4] in ("GLOBAL", "WEAK") \
                and fields[5] == "DEFAULT" and fields[6].isdigit():
            found[name] += 1
    if any(count != 1 for count in found.values()):
        raise ValueError("Missing/ambiguous original provider dynamic function export")
    return EXPORTS


def inspect(path, imported_library, imported_include, common):
    fail, digest, regular = common.fail, common.digest, common.regular
    if not re.fullmatch(r"[0-9a-f]{64}", MANIFEST_SHA) or not re.fullmatch(r"[0-9a-f]{64}", RECORD_SHA):
        fail("Provider candidate has not received an exact reviewed approval")
    if digest(regular(path)) != MANIFEST_SHA:
        fail("Provider manifest is outside the exact catalog")
    manifest = json.loads(path.read_bytes())
    if (set(manifest) != common.MANIFEST_KEYS or type(manifest["schema"]) is not int
            or manifest["schema"] != 1 or manifest["purpose"] != "source-bound-qualification-only"
            or manifest["causal_qualified"] is not False
            or manifest["source_build_record"] != "provider-source-build-record.json"
            or manifest["source_build_record_sha256"] != RECORD_SHA):
        fail("Invalid exact provider manifest")
    record_path = regular(path.parent / "provider-source-build-record.json")
    parent_path = regular(path.parent / "source-build-record.json")
    if digest(record_path) != RECORD_SHA or digest(parent_path) != PARENT_SHA:
        fail("Changed provider or original parent record")
    record, parent = [json.loads(p.read_bytes()) for p in (record_path, parent_path)]
    if (set(record) != RECORD_KEYS or type(record["schema"]) is not int or record["schema"] != 1
            or record["status"] != "SOURCE_BUILT_PROVIDER_CANDIDATE_NOT_RUNTIME_QUALIFIED"
            or record["causal_qualified"] is not False or record["parent_record_sha256"] != PARENT_SHA
            or record["upstream_commit"] != "740a0b9aeabe00e762cc0efe4a0f27593db2550b"
            or record["overlay_manifests_sha256"] != OVERLAYS):
        fail("Wrong provider source cohort")
    roots = {key: common.canonical_directory(manifest[key]) for key in ("source_root", "build_root", "install_root")}
    if any(record[key] != str(value) for key, value in roots.items()):
        fail("Provider source/build/install roots changed")
    source, build, install = [roots[key] for key in ("source_root", "build_root", "install_root")]
    candidate = source.parent
    if source != candidate / "source" or build != candidate / "build" or install != candidate / "install":
        fail("Provider candidate roots are not the exact isolated layout")
    maps = [("source_inputs_sha256", source, 1072, FULL_SOURCE_MAP_SHA),
            ("source_sha256", source, 449, SOURCE_MAP_SHA),
            ("source_build_rules_sha256", source, 13, RULES_MAP_SHA),
            ("bundled_dependency_sha256", source, 16, BUNDLED_MAP_SHA)]
    for key, root, count, expected in maps:
        if map_digest(record[key]) != expected:
            fail("Unexpected provider input map: " + key)
        common.verify_map(record, key, root, count)
    if (record["source_build_rules_sha256"] != parent["source_build_rules_sha256"]
            or record["bundled_dependency_sha256"] != parent["bundled_dependency_sha256"]
            or tree_keys(source) != set(record["source_inputs_sha256"])):
        fail("Changed rules/dependencies or unrecorded source leaf")
    if (map_digest(sorted(record["installed_sha256"])) != INSTALL_KEYS_SHA
            or map_digest(sorted(record["build_files_sha256"])) != BUILD_KEYS_SHA
            or tree_keys(install, True) != set(record["installed_sha256"])):
        fail("Unexpected install/build evidence inventory")
    common.verify_map(record, "installed_sha256", install, 284)
    common.verify_map(record, "build_files_sha256", build, 4)
    for name, expected in record["installed_sha256"].items():
        if name.startswith("include/") and record["source_inputs_sha256"].get(name) != expected:
            fail("Installed SDK header differs from the verified source")
    cache = cache_values(build / "CMakeCache.txt")
    if record["cache"] != cache:
        fail("Changed complete provider CMake cache")
    required_cache = {key: parent["cache"][key] for key in (
        "BUILD_SHARED_LIBS", "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_STANDARD",
        "CMAKE_CXX_STANDARD_REQUIRED", "CMAKE_CXX_EXTENSIONS", "CMAKE_CXX_FLAGS",
        "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_SHARED_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS_RELEASE",
        "build_examples", "build_tools", "deprecated-functions", "developer-options", "dht",
        "encryption", "exceptions", "extensions", "gnutls", "i2p", "logging", "mutable-torrents",
        "python-bindings", "python-egg-info", "python-install-system-dir", "static_runtime", "streaming")}
    required_cache.update(build_tests="ON", CMAKE_HOME_DIRECTORY=str(source), libtorrent_SOURCE_DIR=str(source),
                          libtorrent_BINARY_DIR=str(build), CMAKE_CACHEFILE_DIR=str(build), CMAKE_INSTALL_PREFIX=str(install))
    if any(cache.get(key) != value for key, value in required_cache.items()):
        fail("Provider build configuration differs from the reviewed source ABI")
    artifact = regular(Path(manifest["artifact"]))
    if (artifact != artifact.resolve(strict=True) or artifact != install / "lib/libtorrent-rasterbar.so.2.0.12"
            or str(artifact) != record["artifact"] or manifest["artifact_sha256"] != record["artifact_sha256"]
            or type(manifest["artifact_bytes"]) is not int or manifest["artifact_bytes"] <= 0
            or artifact.stat().st_size != manifest["artifact_bytes"] or record["artifact_bytes"] != manifest["artifact_bytes"]
            or digest(artifact) != manifest["artifact_sha256"]):
        fail("Changed complete provider artifact")
    if imported_library.resolve(strict=True) != artifact or imported_include.resolve(strict=True) != install / "include":
        fail("Imported LT library/SDK differs from this exact provider")
    for field in ("build_receipt", "build_source_map"):
        evidence = regular(Path(record[field]))
        if evidence.parent != candidate or evidence != evidence.resolve(strict=True) \
                or digest(evidence) != record[field + "_sha256"]:
            fail("Changed provider build provenance")
    if json.loads(Path(record["build_source_map"]).read_bytes()) != record["source_sha256"]:
        fail("Actual pre-build source map differs from the approved source")
    receipt = json.loads(Path(record["build_receipt"]).read_bytes())
    if not isinstance(receipt, list) or len(receipt) != 3:
        fail("Missing actual configure/build/install stages")
    for entry, stage in zip(receipt, ("configure", "build", "install")):
        if entry.get("stage") != stage or type(entry.get("exit")) is not int or entry["exit"] != 0:
            fail("Incomplete or failed actual provider build")
        log = regular(candidate / (stage + "-r3.log"))
        if record["logs_sha256"].get(str(log)) != digest(log) or entry.get("log_sha256") != digest(log):
            fail("Changed actual provider build log")
    if len(record["logs_sha256"]) != 3:
        fail("Unexpected provider build-log map")
    scans = [("provider-dynamic-symbols.txt", "export_scan_sha256"), ("provider-dynamic-section.txt", "dynamic_section_sha256")]
    for name, field in scans:
        if digest(regular(path.parent / name)) != record[field]:
            fail("Changed static provider ELF inspection")
    exported_functions((path.parent / "provider-dynamic-symbols.txt").read_text())
    sonames = re.findall(r"\(SONAME\).*\[([^\]]+)\]", (path.parent / "provider-dynamic-section.txt").read_text())
    if record["required_exports"] != EXPORTS or record["elf_soname"] != "libtorrent-rasterbar.so.2.0" \
            or sonames != [record["elf_soname"]]:
        fail("Wrong original provider exports or SONAME")
    return manifest, MANIFEST_SHA
