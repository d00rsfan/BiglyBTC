"""Exact next-provider proposal. UNAPPROVED until root replaces one digest.

No loader, compiler, network, target execution or caller-selected authority.
The old provider verifier and every old catalog branch remain unchanged.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

MANIFEST_SHA = "3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"
RECORD_SHA = '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca'
PARENT_SHA = '34bbba203833a31e59f0e9101f232fc2aaebe2bf080bdb8b640a29d68bd4016f'
PREVIOUS_VERIFIER_SHA = 'fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a'
CANDIDATE_ROOT = '/home/yurii/Java/BiglyBT/build/storage-20261025/provider-next'
ARTIFACT_SHA = 'b0e55bfbdc2be5dc2ebbefc953837d64c9e5151b0305332335071e37b6956937'
SOURCE_MAP_SHA = '2f5e335795d8f9c26998833f4ce48694f508e129dfd5a196df7bcd48bea5be1b'
FULL_SOURCE_MAP_SHA = '74fe9c2864a63bcff63a1cb5eb7b13f715cd2df0177365a627bd717bfc0dea5d'
RULES_MAP_SHA = '33382d248a5704ded57ac4888a6ec619eadd895538add123a47e58d5bb8a782b'
BUNDLED_MAP_SHA = 'df8884e03b9fe1572db421d222cacb167f8be25b7be162d7bda9711a3d0827a0'
INSTALL_KEYS_SHA = '902c8946ff38d717127c36abc9e099103a8d24723fbbfec07a9239d32c4b406a'
BUILD_KEYS_SHA = '286ffe037b351047d9c9c2e2c29bafdbe301c4a17f4c6718c6e6f0ab8149d270'
CHANGES = {'include/libtorrent/biglybtc_payload_counters.hpp': '04fb225edd0a3ef42e3bab6d1938aeb4d195a060825707bc4d7582879c5f0915', 'include/libtorrent/torrent_handle.hpp': '99d1cc4d850af0308970d63dea3d534b85d605a6173c18690625f36bf6b8bc95', 'src/torrent_handle.cpp': '4da6f107103c956eb9a782ea0ea61ad1b7043ac06319e541fd3f59949feb188e', 'test/test_biglybtc_traffic_counters.cpp': '2d8c18cbb0619c257319d1c3fdefc8aba03f6a31f07a3d47525fca20d4bd81ba', 'include/libtorrent/aux_/proxy_settings.hpp': 'b6266343e687aedeb5f468e48dafc0a4656b2293d8025af07cf9a7601ab913fe', 'include/libtorrent/biglybtc_socks4a.hpp': '4a3d2fbd1f7bf7682784fae4a1825c80268739acb4d45f5f9e16a913999b68f9', 'include/libtorrent/settings_pack.hpp': '0730a635e38975fa728672b2e484e107ae510d910a7deb137732152a5d40a148', 'include/libtorrent/socks5_stream.hpp': '4444715ce0d17139f665321ca9d9dbed9b06a2ee14fc31d2b359ae68179a8b3d', 'src/http_connection.cpp': 'a446dc2e042ee140b6bc27099cac690cbb8361cfe8ce5a1ee7f0ef23d904de9d', 'src/instantiate_connection.cpp': '010edf26a7ba73b5584d884522ab2cc6da173a0d845bf37054232ad8569142dc', 'src/socks5_stream.cpp': '04f928a95422eb65979cb8500b191d71694a226cf97f94ac7914aa635ecd852b'}
BEFORE_CHANGES = {'include/libtorrent/biglybtc_payload_counters.hpp': '27b3386a554f9d2df1f55c8cbd3546208e794d7534803e10ad530d64b266edeb', 'include/libtorrent/torrent_handle.hpp': '2318ed38915b7fcbc61c27c5091a0827d14d175d1ae1d9ef340d5d6efb1ec2a4', 'src/torrent_handle.cpp': '52ef0e1c011caa9209cdafd9bd206e15ea23affe92a23b52e95e970cf7fd2410', 'test/test_biglybtc_traffic_counters.cpp': None, 'include/libtorrent/aux_/proxy_settings.hpp': 'e9c06a2dc2a2c486fcb928e98e5b71383c3468e710ed11bde19601a8e7658833', 'include/libtorrent/biglybtc_socks4a.hpp': None, 'include/libtorrent/settings_pack.hpp': '101e5bd1561765eedfe498e753cc23cb66611bf8a47b66455497fb91e7514d67', 'include/libtorrent/socks5_stream.hpp': 'ae4f116b943d01ed16ed8b62fe298fe92d379c82720b057ed33e7deb8e2a5f13', 'src/http_connection.cpp': 'd11c7d8546d7e5d1111fca069a5c30d6bc299ffd596021c97ed599dd8d2786f0', 'src/instantiate_connection.cpp': '5d5cb4a183505bffef8b91ca3cf140e4ae9bae61ecb0cc5c47c6436112818a56', 'src/socks5_stream.cpp': 'dfd77c9c61cc719e0af526400990be48f5c7fd1046544b4cf60e85b107fce2e1'}
OVERLAYS = {'socks4a': '6a61e5644b78dbe57d80a26ee679a6b4bab3e6ca8e58976bb6c53f656b4c4414', 'traffic_v2': 'd64c8eac8094d753e51585e04aa9336562ddef05b2ebe45df9a8ad91c829b8aa'}
EXPORTS = ['_ZN10libtorrent7versionEv', '_ZN10libtorrent21session_stats_metricsEv', '_ZN10libtorrent14session_handle18post_session_statsEv', '_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE', '_ZNK10libtorrent19session_stats_alert8countersEv', '_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE', 'biglybtc_swarm_mask_abi_v1', 'biglybtc_payload_counter_abi_v1', 'biglybtc_read_payload_counters_v1', 'biglybtc_socks4a_abi_v1', 'biglybtc_traffic_counter_abi_v2', 'biglybtc_read_traffic_counters_v2']
DIRECT_TEST_NAMES = ['test_biglybtc_swarm', 'test_biglybtc_utf16_info', 'test_biglybtc_traffic_counters']
RECORD_KEYS = set(['actual_socks_qt_rows', 'approval_or_catalog_created', 'artifact', 'artifact_bytes', 'artifact_sha256', 'build_files_sha256', 'build_root', 'bundled_dependency_sha256', 'cache', 'capsule_manifest_sha256', 'causal_qualified', 'continuation_baseline_sha256', 'continuation_recipe_sha256', 'direct_provider_test_names', 'dynamic_section_sha256', 'elf_soname', 'export_scan_sha256', 'install_root', 'installed_sha256', 'native_runtime_approved', 'overlay_manifests_sha256', 'parent_record_sha256', 'r1_commands', 'r1_commands_sha256', 'r1_failed_cache_preserved', 'r1_provider_rebuilt_or_retested', 'r1_provider_tests_xml_sha256', 'r2_commands', 'r2_commands_sha256', 'r3_commands', 'r3_commands_sha256', 'r3_socks_tests_xml_sha256', 'required_exports', 'schema', 'source_build_rules_sha256', 'source_inputs_sha256', 'source_plan_sha256', 'source_root', 'source_sha256', 'status', 'upstream_commit'])
EVIDENCE_PINS = {'manifest.json': 'ff3e45844f7005d18a0a11ade922ab7ce90dd3660ad884cc4474cf5fb2ce6969', 'source-plan.json': '0e55fa7bd1f0c6a72f6610d89a2c97b524d340932a618a0abaeabd35314fa6b1', 'sdk-plan.json': '1d3cd042a00aa1b19a873f77d30ab1be256a5f1dae9ca22ff9fb0d484ee643ea', 'build_candidate.py': '2b67a9bdfec0f96f83c29a1dfd3d8136e7a1d5c1295af1d7898bb138909c59cd', 'continuations/manifest.json': '5c31a313caf6f088f3135331bd09883bef4ac834511974256ef3df5527222923', 'continuations/ADOPTION.md': 'b3bd753bbd61f335d36ff3f50bdfa398e3cd130f0d8cfff1a1e10d7e072912ce', 'continuations/r2/continue_socks.py': '19ae6a11a619364e70948dd98632e0f04169a5a50cbe7b5e45ff7a1121f07d16', 'continuations/r2/execution/baseline.json': '70722204e5d0cbc9f8484047843ae274820cf02900f184d8006742309232cb7c', 'continuations/r2/execution/commands.json': '7c76215cf1de0cc200a7def066f6f413a3349b8f22f82d1f39d10b9ff48dea17', 'continuations/r2/execution/socks-build.log': 'e5e48f7425c9633e737eaa05bf0ef499e4f7211c0710bd9177bf6c3d0331c981', 'continuations/r2/execution/socks-configure.log': '57997fa08f757c304d8e1e4140565eecc0cb9b0ba0ce8168a5741fb3e2894b8e', 'continuations/r2/execution/socks-tests.log': 'ecf0d972004627a345b242bc37e234070896bb4c3fe5a568d4aa92354a9ba17a', 'continuations/r2/execution/socks-tests.xml': '9560f44e6683e4f4766826c01938760f2a0728b442526a0f2aef6a359cb7f32a', 'continuations/r2/fixture/CMakeLists.txt': '9a5126e172708c76771656b9dac8a92273e221f5999164e5e8a224f948bef129', 'continuations/r2/fixture/socks4a_provider_tests.cpp': 'fe57cbc46868fa5ce2cb80909bae0af2fe639bf822ae0cc35ed0170adda77c90', 'continuations/r3/continue_test.py': '9a188b29d1ba95f7ee9bd658148324d6fd7bf7dedb612a98d70066a2a7068d8f', 'continuations/r3/execution/baseline.json': '61dcc96b900ecc25a21cf2f73d2973443b4d3f565300e1bdbeb958f2a3afc30d', 'continuations/r3/execution/commands.json': '4dbb0b1e7c17fb9fb880351b63ca66b4fa635abe9da29989c48fe1dcab2d5432', 'continuations/r3/execution/dynamic.txt': '9a16e7311bcfc4ba9ca3121b66506cefc3c0a3d234450ee3c8ea5e19637a211d', 'continuations/r3/execution/exports.txt': '99dc6e23064cc7907455dd9dbde934cb2b814597792c52b3782c41204113e04e', 'continuations/r3/execution/loader.txt': 'eb36d25697f0a6c48d1c7b803b437e95af57ca03e2bbf26719f88fe1a3ec7245', 'continuations/r3/execution/socks-tests.log': '16a0e717dbde0d5b192b5308e4efc2c0ebd1871da01d4c23bcdbf4451a13e604', 'continuations/r3/execution/socks-tests.xml': '63d184f033fd1ba9b65d08234c6512102d05c39696685f77ce4c42f0a8707a15', 'continuations/r3/r2-Testing-before-reuse/Temporary/CTestCostData.txt': '51188451294749ba462eca6a31d93e060b27bda54db3a7dfc730208a267c4925', 'continuations/r3/r2-Testing-before-reuse/Temporary/LastTest.log': '2d7e008bf5e7eccd918e5b9006e7620d68ce7de40d0444a75a9e19bccbe1c524', 'continuations/r3/r2-Testing-before-reuse/Temporary/LastTestsFailed.log': '5db32f2052dd1e3876d558a7a19186ccd40639e5943fcaf8cd6097ef48411799', 'execution-r1/provider-tests.xml': '162dd6c9b4020f00ebdae38f77a3cebab50125434581c436bacf781e03e90d35', 'execution-r1/socks-configure.log': '751c99655d536d9659b650beca7bf9b875bd60748c4bc87521d94a6ea0250f39', 'execution-r1/install.log': '8e51a21f4570f1af956ff1afc0ef5805096344c8dc07cb061b61aed763058791', 'execution-r1/provider-direct-tests.log': '7a82451702e28d3862da1b330fda5e999225062cc37a95360c6e05b77a21f19a', 'execution-r1/build.log': '957fba54ed47adcc591dd1f02e22c138358ada4945042ce9448e7b1f84749a4e', 'execution-r1/configure.log': '7330b2d5a5daf47157cc5b2f91b4a8510456864b80b745316631db07e6d95dec', 'execution-r1/commands.json': '40253bf84385b1169a9fc62266e3b1dbc13238313e4181adb53e6d23ca57585d'}
EXTERNAL_INPUT_PINS = {'next-queue/combined-counters/provider-manifest.json': 'd64c8eac8094d753e51585e04aa9336562ddef05b2ebe45df9a8ad91c829b8aa', 'next-network/socks4a/manifest.json': '6a61e5644b78dbe57d80a26ee679a6b4bab3e6ca8e58976bb6c53f656b4c4414'}


def map_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def previous(common):
    path = common.regular(Path(__file__).with_name("verify_provider_runtime.py"))
    if common.digest(path) != PREVIOUS_VERIFIER_SHA:
        common.fail("Changed previous provider verifier")
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("biglybtc_previous_provider_helpers", path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def command_chain(candidate, record, common):
    """Preserve actual r1 success/failure, r2 setup/failure and r3 success."""
    all_commands = []
    for field, relative, names, exits in (
            ("r1_commands", "execution-r1/commands.json",
             ["configure", "build", "install", "provider-direct-tests", "socks-configure"], [0, 0, 0, 0, 1]),
            ("r2_commands", "continuations/r2/execution/commands.json",
             ["socks-configure", "socks-build", "socks-tests"], [0, 0, 8]),
            ("r3_commands", "continuations/r3/execution/commands.json", ["socks-tests"], [0])):
        path = common.regular(candidate / relative)
        if record[field] != str(path) or common.digest(path) != record[field + "_sha256"]:
            common.fail("Changed actual command chain: " + field)
        commands = json.loads(path.read_bytes())
        if (not isinstance(commands, list) or len(commands) != len(names)
                or [entry.get("name") for entry in commands] != names):
            common.fail("Unexpected actual provider command sequence")
        for entry, expected in zip(commands, exits):
            log = common.regular(path.parent / (entry["name"] + ".log"))
            if (type(entry.get("exit_code")) is not int or entry["exit_code"] != expected
                    or entry.get("timed_out") is not False or common.digest(log) != entry.get("log_sha256")
                    or not isinstance(entry.get("argv"), list) or not entry["argv"]):
                common.fail("Changed/incomplete actual provider command or log")
        all_commands.append(commands)
    # No synthetic configure/build/install receipt is manufactured. These are
    # the exact historical attempts, including both failures, not reruns.
    for relative, names, field in (
            ("execution-r1/provider-tests.xml", DIRECT_TEST_NAMES, "r1_provider_tests_xml_sha256"),
            ("continuations/r3/execution/socks-tests.xml", ["socks4a-provider"], "r3_socks_tests_xml_sha256")):
        path = common.regular(candidate / relative)
        tests = ET.fromstring(path.read_bytes()).findall(".//testcase")
        if (common.digest(path) != record[field] or len(tests) != len(names)
                or {case.get("name") for case in tests} != set(names)
                or any(case.findall(tag) for case in tests for tag in ("failure", "error", "skipped"))):
            common.fail("Incomplete actual provider direct tests")
    log = (candidate / "continuations/r3/execution/socks-tests.log").read_text()
    if re.findall(r"Totals:\s+(\d+) passed,\s+0 failed,\s+0 skipped", log) != ["6"]:
        common.fail("Missing actual four SOCKS rows plus init/cleanup")
    expected_rows = ["numeric4-USERID", "remote4a-unresolvable-HTTP", "remote4a-localhost-HTTP", "remote4a-refused"]
    if any("PASS   : Socks4aProviderTests::actualHttpFactoryRoutes(" + row + ")" not in log for row in expected_rows):
        common.fail("Missing actual SOCKS route case")
    loader = (candidate / "continuations/r3/execution/loader.txt").read_text()
    if str(candidate / "install/lib/libtorrent-rasterbar.so.2.0") not in loader or "not found" in loader:
        common.fail("Actual SOCKS test loaded another or missing provider")


def inspect(path, imported_library, imported_include, common):
    fail, digest, regular = common.fail, common.digest, common.regular
    if not re.fullmatch(r"[0-9a-f]{64}", MANIFEST_SHA):
        fail("Next provider remains UNAPPROVED; root exact review/catalog amendment required")
    if digest(regular(path)) != MANIFEST_SHA:
        fail("Next-provider manifest is outside the exact reviewed catalog")
    manifest = json.loads(path.read_bytes())
    if (set(manifest) != common.MANIFEST_KEYS or type(manifest.get("schema")) is not int
            or manifest["schema"] != 1 or manifest["purpose"] != "source-bound-qualification-only"
            or manifest["causal_qualified"] is not False
            or manifest["source_build_record"] != "provider-next-source-build-record.json"
            or manifest["source_build_record_sha256"] != RECORD_SHA):
        fail("Invalid exact next-provider manifest")
    record_path = regular(path.parent / "provider-next-source-build-record.json")
    parent_path = regular(path.parent / "provider-source-build-record.json")
    if digest(record_path) != RECORD_SHA or digest(parent_path) != PARENT_SHA:
        fail("Changed next-provider or exact original provider parent record")
    record, parent = [json.loads(value.read_bytes()) for value in (record_path, parent_path)]
    if (set(record) != RECORD_KEYS or type(record["schema"]) is not int or record["schema"] != 1
            or record["status"] != "BUILT_AND_DIRECTLY_TESTED_CANDIDATE_NOT_APPROVED"
            or any(record[key] is not False for key in (
                "causal_qualified", "native_runtime_approved", "approval_or_catalog_created", "r1_provider_rebuilt_or_retested"))
            or record["r1_failed_cache_preserved"] is not True
            or record["parent_record_sha256"] != PARENT_SHA
            or record["upstream_commit"] != "740a0b9aeabe00e762cc0efe4a0f27593db2550b"
            or record["overlay_manifests_sha256"] != OVERLAYS
            or record["direct_provider_test_names"] != DIRECT_TEST_NAMES
            or type(record["actual_socks_qt_rows"]) is not int or record["actual_socks_qt_rows"] != 6):
        fail("Wrong exact next-provider source/history cohort")
    roots = {key: common.canonical_directory(manifest[key]) for key in ("source_root", "build_root", "install_root")}
    if any(record[key] != str(value) for key, value in roots.items()):
        fail("Changed source/build/install roots")
    source, build, install = [roots[key] for key in ("source_root", "build_root", "install_root")]
    candidate = source.parent
    if str(candidate) != CANDIDATE_ROOT or (source, build, install) != tuple(candidate / name for name in ("source", "build", "install")):
        fail("Unexpected exact next-provider owned layout")
    previous_verifier = previous(common)
    expected_full, expected_selected = dict(parent["source_inputs_sha256"]), dict(parent["source_sha256"])
    for name, before in BEFORE_CHANGES.items():
        if expected_full.get(name) != before or (before is not None and expected_selected.get(name) != before):
            fail("Unexpected original provider beforeimage: " + name)
    expected_full.update(CHANGES)
    expected_selected.update(CHANGES)
    if record["source_inputs_sha256"] != expected_full or record["source_sha256"] != expected_selected:
        fail("Source is not the exact parent plus the eleven independently pinned delta paths")
    for key, root, count, expected in (
            ("source_inputs_sha256", source, 1074, FULL_SOURCE_MAP_SHA),
            ("source_sha256", source, 451, SOURCE_MAP_SHA),
            ("source_build_rules_sha256", source, 13, RULES_MAP_SHA),
            ("bundled_dependency_sha256", source, 16, BUNDLED_MAP_SHA)):
        if map_digest(record[key]) != expected:
            fail("Unexpected closed next-provider map: " + key)
        common.verify_map(record, key, root, count)
    if (record["source_build_rules_sha256"] != parent["source_build_rules_sha256"]
            or record["bundled_dependency_sha256"] != parent["bundled_dependency_sha256"]
            or previous_verifier.tree_keys(source) != set(record["source_inputs_sha256"])):
        fail("Changed original rules/dependencies or unrecorded source leaf")
    if (map_digest(sorted(record["installed_sha256"])) != INSTALL_KEYS_SHA
            or map_digest(sorted(record["build_files_sha256"])) != BUILD_KEYS_SHA
            or previous_verifier.tree_keys(install, True) != set(record["installed_sha256"])):
        fail("Unexpected complete install/build inventory")
    common.verify_map(record, "installed_sha256", install, 285)
    common.verify_map(record, "build_files_sha256", build, 4)
    headers = {name: expected for name, expected in record["installed_sha256"].items() if name.startswith("include/")}
    if len(headers) != 276 or any(record["source_inputs_sha256"].get(name) != expected for name, expected in headers.items()):
        fail("Installed SDK differs from the source-built header cohort")
    typed_cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")):
            key, separator, value = line.partition("=")
            if not separator or ":" not in key or key in typed_cache:
                fail("Malformed or duplicate actual typed CMake cache")
            typed_cache[key] = value
    if record["cache"] != typed_cache:
        fail("Changed complete next-provider CMake cache")
    cache = previous_verifier.cache_values(build / "CMakeCache.txt")
    abi_keys = ("BUILD_SHARED_LIBS", "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_STANDARD",
                "CMAKE_CXX_STANDARD_REQUIRED", "CMAKE_CXX_EXTENSIONS", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE",
                "CMAKE_SHARED_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS_RELEASE", "build_examples", "build_tools",
                "deprecated-functions", "developer-options", "dht", "encryption", "exceptions", "extensions", "gnutls",
                "i2p", "logging", "mutable-torrents", "python-bindings", "python-egg-info", "python-install-system-dir",
                "static_runtime", "streaming")
    if any(cache.get(key) != parent["cache"][key] for key in abi_keys) or cache.get("build_tests") != "ON":
        fail("Source ABI build controls changed")
    artifact = regular(Path(manifest["artifact"]))
    if (artifact != artifact.resolve(strict=True) or artifact != install / "lib/libtorrent-rasterbar.so.2.0.12"
            or str(artifact) != record["artifact"] or manifest["artifact_sha256"] != ARTIFACT_SHA
            or record["artifact_sha256"] != ARTIFACT_SHA or type(manifest["artifact_bytes"]) is not int
            or manifest["artifact_bytes"] != 9085072 or record["artifact_bytes"] != 9085072
            or artifact.stat().st_size != 9085072 or digest(artifact) != ARTIFACT_SHA):
        fail("Changed complete next-provider artifact")
    if imported_library.resolve(strict=True) != artifact or imported_include.resolve(strict=True) != install / "include":
        fail("Imported LT library/SDK differs from this exact next provider")
    for relative, expected in EVIDENCE_PINS.items():
        if digest(regular(candidate / relative)) != expected:
            fail("Changed actual provider evidence: " + relative)
    for relative, expected in EXTERNAL_INPUT_PINS.items():
        if digest(regular(candidate.parent / relative)) != expected:
            fail("Changed frozen source recipe: " + relative)
    if (record["capsule_manifest_sha256"] != EVIDENCE_PINS["manifest.json"]
            or record["source_plan_sha256"] != EVIDENCE_PINS["source-plan.json"]
            or record["continuation_baseline_sha256"] != EVIDENCE_PINS["continuations/r3/execution/baseline.json"]
            or record["continuation_recipe_sha256"] != EVIDENCE_PINS["continuations/r3/continue_test.py"]):
        fail("Wrong exact continuation or source preparation recipe")
    baseline = json.loads((candidate / "continuations/r3/execution/baseline.json").read_bytes())
    for name, expected in baseline["failed_socks_cache_sha256"].items():
        if digest(regular(candidate / "socks4a-build" / name)) != expected:
            fail("Historical failed r1 SOCKS cache changed")
    command_chain(candidate, record, common)
    scan = (candidate / "continuations/r3/execution/exports.txt").read_text()
    counts = {name: 0 for name in EXPORTS}
    for line in scan.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in ("T", "W") and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            name = fields[2].split("@", 1)[0]
            if name in counts:
                counts[name] += 1
    dynamic = (candidate / "continuations/r3/execution/dynamic.txt").read_text()
    if (any(value != 1 for value in counts.values()) or record["required_exports"] != EXPORTS
            or record["export_scan_sha256"] != EVIDENCE_PINS["continuations/r3/execution/exports.txt"]
            or record["dynamic_section_sha256"] != EVIDENCE_PINS["continuations/r3/execution/dynamic.txt"]
            or record["elf_soname"] != "libtorrent-rasterbar.so.2.0"
            or re.findall(r"\(SONAME\).*\[([^\]]+)\]", dynamic) != [record["elf_soname"]]):
        fail("Changed actual provider exports or SONAME")
    return manifest, MANIFEST_SHA
