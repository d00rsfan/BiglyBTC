#!/usr/bin/env python3
"""Additive exact private webseed provider binding; old catalog is byte-exact.

No target, loader, compiler, subprocess, network or caller-selected authority.
Root must review/adopt this source capsule; file presence is not runtime credit.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

MANIFEST_SHA = 'ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5'
RECORD_SHA = 'a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08'
PARENT_SHA = '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca'
CANDIDATE = '/home/yurii/Java/BiglyBT/build/compatibility-20261026/provider-webseed'
ARTIFACT_SHA = 'd33c17c73c3c377cedd88256b0b48e472b2de55f0a95a890e2837556e1d1002a'
ARTIFACT_BYTES = 9085128
PARENT_HELPERS = {'verify_runtime_manifest.py': 'db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c', 'verify_provider_next_runtime.py': '0922c9960a507c0b453b240ba3ca5b56c595eaf256fbcf5ce4bcf2d968b88e32', 'verify_provider_runtime.py': 'fc9ae8203bb6f32f94d57a05c5ea174944bdf1dbbe41ae51d9caf987ac01d89a'}
RECORD_KEYS = ['artifact', 'artifact_bytes', 'artifact_sha256', 'build_files_sha256', 'build_root', 'bundled_dependency_sha256', 'capsule_manifest_sha256', 'causal_qualified', 'commands_sha256', 'direct_qualification', 'dynamic_section_sha256', 'export_scan_sha256', 'install_root', 'installed_sha256', 'native_runtime_approved', 'parent_record_sha256', 'recipe_sha256', 'required_exports', 'schema', 'source_build_rules_sha256', 'source_inputs_sha256', 'source_plan_sha256', 'source_root', 'source_sha256', 'status', 'upstream_commit']
BEFORE = {'include/libtorrent/biglybtc_socks4a.hpp': '4a3d2fbd1f7bf7682784fae4a1825c80268739acb4d45f5f9e16a913999b68f9', 'src/socks5_stream.cpp': '04f928a95422eb65979cb8500b191d71694a226cf97f94ac7914aa635ecd852b', 'src/torrent.cpp': '6d0e2cafa2274caa13cbe713fbdbe841e10bda88edf065c8bddea4d7e7bcdba4'}
CHANGES = {'include/libtorrent/biglybtc_socks4a.hpp': 'c8f8d4ae78bbac8d63cadb2bcb82dda0656d9ea0c71a2a65f700a13321b6857b', 'src/socks5_stream.cpp': '92cf3d06da777f1c8a67ddd8e1fff860011067ecb4b189ae54f9092f3de1402c', 'src/torrent.cpp': '7703ce43573edb8edfbc472d3f4004977f6a1fdbb3b87d3f64265503e03e11e1'}
CLOSED_MAPS = {'source_inputs_sha256': '181e80b3451633ec0526f133960d10e252cd8c84b21cd43e0a051d29f28ed160', 'source_sha256': 'a7748eef9a4e126ca84198c4a712ef51657b0c84dddf165968779545675c153f', 'source_build_rules_sha256': '33382d248a5704ded57ac4888a6ec619eadd895538add123a47e58d5bb8a782b', 'bundled_dependency_sha256': 'df8884e03b9fe1572db421d222cacb167f8be25b7be162d7bda9711a3d0827a0', 'installed_sha256': '0e4e65e0cef4a5a195f5752c3f1714d91474ddc34791496b73d0d87b7a038f67', 'build_files_sha256': '5f2dba1c223467e86c120489e938e472088bfcfe2c6740a092e144eaa9497fe8'}
EXPORTS = ['_ZN10libtorrent7versionEv', '_ZN10libtorrent21session_stats_metricsEv', '_ZN10libtorrent14session_handle18post_session_statsEv', '_ZN10libtorrent14session_handle10pop_alertsEPSt6vectorIPNS_5alertESaIS3_EE', '_ZNK10libtorrent19session_stats_alert8countersEv', '_ZN10libtorrent14session_handle14remove_torrentERKNS_14torrent_handleENS_5flags13bitfield_flagIhNS_16remove_flags_tagEvEE', 'biglybtc_swarm_mask_abi_v1', 'biglybtc_payload_counter_abi_v1', 'biglybtc_read_payload_counters_v1', 'biglybtc_socks4a_abi_v1', 'biglybtc_traffic_counter_abi_v2', 'biglybtc_read_traffic_counters_v2', 'biglybtc_socks4a_webseed_abi_v1']
EVIDENCE = {'build_candidate.py': '416450691eb9a2d0b8dc397baf78ed07a2d8cb94b1ac13593e307f784e441713', 'execution-r1/build.log': '1e899f9dc5d60c32c15ef32932f029dfa5a6140053c98e95acf44e6d27ec5f59', 'execution-r1/candidate-source-build-record.json': 'a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08', 'execution-r1/commands.json': '6b1ef1c7a8fe1b0718071c45851c13aec8c045931d3585490ff5af6afa60b859', 'execution-r1/configure.log': '3b5504f63c2695bb6ccf677f6e8c72906c41b71ad648f60b0f75f3e897e5cfaa', 'execution-r1/dynamic.txt': '69a71eae4cf99b6b19d67d8b24820680a9df3c4f1ec30b030525d92268fb54ce', 'execution-r1/exports.txt': 'dc5fe678d9fafc86feae810f5447163a7b5648a42ec654800aa19f324efb105a', 'execution-r1/install.log': '181b32800e8b2813ba3ace558f2b9ccd330e86f9d26851cf9991198fb87c1126', 'execution-r1/provider-direct-tests.log': '85372566ce96080f534dde54069a793241c3034d7a0e136a0ffbf824de524768', 'execution-r1/provider-tests.xml': '7b13913f9c9c97672624c254430c33cd2a40f2127e19616d1b69d970058ea56b', 'execution-r1/socks-build.log': 'e5e48f7425c9633e737eaa05bf0ef499e4f7211c0710bd9177bf6c3d0331c981', 'execution-r1/socks-configure.log': '8f3349992ab4725ef3fcc78b5bf9e68a20cea8bd5be52c7fae9c145148ca6c62', 'execution-r1/socks-tests.log': '2ffc82198b8109ef7af761fdfb7f9f8a133e5769d75e4aa84b6890a4630d75e2', 'execution-r1/socks-tests.xml': '03fd365c0081b341d89a8ee333de5c6006bfcb6db9951733e8d47a112d13c782', 'manifest.json': '21112b39247c54cf3365823caed85cad4eec201f15a64109b37fc9ba1a277861', 'parent-record.json': '3fcd415424d71e6082022f8be6bb1a75036a0fbf3b228228813e1a4cee1ffdca', 'root-review-r1.json': '48d64aecfd857969b4253b61254130bebe8b491a8a7e6bdbd88e514766662a8a', 'sdk-plan.json': '7137348842cad326bbbeac0d28dcf4f9aa3a748e3a76c87cc67c2da005a7f2df', 'socks4a-fixture/CMakeLists.txt': '9a5126e172708c76771656b9dac8a92273e221f5999164e5e8a224f948bef129', 'socks4a-fixture/socks4a_provider_tests.cpp': 'fe57cbc46868fa5ce2cb80909bae0af2fe639bf822ae0cc35ed0170adda77c90', 'source-plan.json': 'ca9164ea434e085a74aab34f3382f8026121d6797e1fca39eff9e1a9c5b63d27'}
ROOT_REVIEW_SCRIPT_SHA = 'd26e1a8cdaa837a37fc45b36dfadda256fff52ebc9592fb86fe03ba5ae1b8400'


def map_digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(",",":")).encode()).hexdigest()


def parent_helpers():
    result={}
    sys.dont_write_bytecode=True
    for name,pin in PARENT_HELPERS.items():
        path=Path(__file__).with_name(name)
        if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=pin:
            raise ValueError("Changed pinned previous runtime helper: "+name)
        spec=importlib.util.spec_from_file_location("biglybtc_webseed_parent_"+name.replace(".","_"),path)
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        result[name]=module
    return result


def verify_maps(record,source,build,install,parent,common,helpers):
    expected_full=dict(parent["source_inputs_sha256"])
    expected_selected=dict(parent["source_sha256"])
    for name,before in BEFORE.items():
        if expected_full.get(name)!=before or expected_selected.get(name)!=before:
            common.fail("Webseed parent beforeimage differs: "+name)
    expected_full.update(CHANGES);expected_selected.update(CHANGES)
    if record["source_inputs_sha256"]!=expected_full or record["source_sha256"]!=expected_selected:
        common.fail("Webseed source is not the exact parent plus three pinned changes")
    for key,root,count in (("source_inputs_sha256",source,1074),("source_sha256",source,451),
            ("source_build_rules_sha256",source,13),("bundled_dependency_sha256",source,16),
            ("installed_sha256",install,285),("build_files_sha256",build,4)):
        if map_digest(record[key])!=CLOSED_MAPS[key]:
            common.fail("Changed closed webseed input map: "+key)
        common.verify_map(record,key,root,count)
    old=helpers["verify_provider_runtime.py"]
    if (record["source_build_rules_sha256"]!=parent["source_build_rules_sha256"]
            or record["bundled_dependency_sha256"]!=parent["bundled_dependency_sha256"]
            or old.tree_keys(source)!=set(record["source_inputs_sha256"])
            or old.tree_keys(install,True)!=set(record["installed_sha256"])):
        common.fail("Webseed rules, dependencies or complete tree inventory changed")
    headers={name:pin for name,pin in record["installed_sha256"].items() if name.startswith("include/")}
    if len(headers)!=276 or any(record["source_inputs_sha256"].get(name)!=pin for name,pin in headers.items()):
        common.fail("Webseed installed SDK differs from source-built headers")
    cache=old.cache_values(build/"CMakeCache.txt")
    parent_cache={key.partition(":")[0]:value for key,value in parent["cache"].items()}
    abi=("BUILD_SHARED_LIBS","CMAKE_BUILD_TYPE","CMAKE_CXX_COMPILER","CMAKE_CXX_STANDARD",
         "CMAKE_CXX_STANDARD_REQUIRED","CMAKE_CXX_EXTENSIONS","CMAKE_CXX_FLAGS","CMAKE_CXX_FLAGS_RELEASE",
         "CMAKE_SHARED_LINKER_FLAGS","CMAKE_SHARED_LINKER_FLAGS_RELEASE","deprecated-functions","dht",
         "encryption","exceptions","extensions","gnutls","i2p","logging","mutable-torrents",
         "static_runtime","streaming")
    required={key:parent_cache[key] for key in abi}
    required.update(build_tests="ON",build_examples="OFF",build_tools="OFF",
        CMAKE_HOME_DIRECTORY=str(source),libtorrent_SOURCE_DIR=str(source),libtorrent_BINARY_DIR=str(build),
        CMAKE_CACHEFILE_DIR=str(build),CMAKE_INSTALL_PREFIX=str(install))
    if any(cache.get(key)!=value for key,value in required.items()):
        common.fail("Webseed ABI/source/build/install controls changed")


def verify_evidence(record,candidate,common):
    for relative,pin in EVIDENCE.items():
        if common.digest(common.regular(candidate/relative))!=pin:
            common.fail("Changed actual webseed build/test evidence: "+relative)
    if common.digest(common.regular(candidate.parent/"review_webseed_provider.py"))!=ROOT_REVIEW_SCRIPT_SHA:
        common.fail("Changed independent source/build/SDK review source")
    names=["configure","build","install","provider-direct-tests","socks-configure","socks-build","socks-tests"]
    commands=json.loads((candidate/"execution-r1/commands.json").read_bytes())
    if (record["commands_sha256"]!=EVIDENCE["execution-r1/commands.json"]
            or not isinstance(commands,list) or [row.get("name") for row in commands]!=names):
        common.fail("Wrong actual webseed command chain")
    for row in commands:
        if (type(row.get("exit_code")) is not int or row["exit_code"]!=0 or row.get("timed_out") is not False
                or not isinstance(row.get("argv"),list) or not row["argv"]
                or row.get("log_sha256")!=EVIDENCE["execution-r1/"+row["name"]+".log"]):
            common.fail("Incomplete actual webseed command/log")
    for name,names in (("provider-tests",["test_biglybtc_swarm","test_biglybtc_traffic_counters","test_biglybtc_utf16_info"]),
                       ("socks-tests",["socks4a-provider"])):
        xml="execution-r1/"+name+".xml"
        root=ET.fromstring((candidate/xml).read_bytes());rows=list(root.iter("testcase"))
        if (len(rows)!=len(names) or {row.get("name") for row in rows}!=set(names)
                or any(row.find(tag) is not None for row in rows for tag in ("failure","error","skipped"))
                or record["direct_qualification"][name]!={"names":sorted(names),"xml_sha256":EVIDENCE[xml]}):
            common.fail("Missing exact actual webseed direct test passes")
    log=(candidate/"execution-r1/socks-tests.log").read_text()
    if (re.findall(r"Totals:\s+(\d+) passed,\s+0 failed,\s+0 skipped",log)!=["6"]
            or type(record["direct_qualification"]["socks_qt_rows"]) is not int
            or record["direct_qualification"]["socks_qt_rows"]!=6):
        common.fail("Missing actual SOCKS four rows plus init/cleanup")
    for row in ("numeric4-USERID","remote4a-unresolvable-HTTP","remote4a-localhost-HTTP","remote4a-refused"):
        if "PASS   : Socks4aProviderTests::actualHttpFactoryRoutes("+row+")" not in log:
            common.fail("Missing actual SOCKS route row: "+row)
    scan=(candidate/"execution-r1/exports.txt").read_text();counts=dict.fromkeys(EXPORTS,0)
    for line in scan.splitlines():
        fields=line.split()
        if len(fields)==3 and fields[1] in ("T","W") and re.fullmatch(r"[0-9a-fA-F]+",fields[0]):
            name=fields[2].split("@",1)[0]
            if name in counts:counts[name]+=1
    dynamic=(candidate/"execution-r1/dynamic.txt").read_text()
    if (any(count!=1 for count in counts.values()) or record["required_exports"]!=EXPORTS
            or record["export_scan_sha256"]!=EVIDENCE["execution-r1/exports.txt"]
            or record["dynamic_section_sha256"]!=EVIDENCE["execution-r1/dynamic.txt"]
            or re.findall(r"\(SONAME\).*\[([^\]]+)\]",dynamic)!=["libtorrent-rasterbar.so.2.0"]):
        common.fail("Changed webseed 13 exported functions or SONAME")
    if (record["capsule_manifest_sha256"]!=EVIDENCE["manifest.json"]
            or record["source_plan_sha256"]!=EVIDENCE["source-plan.json"]
            or record["recipe_sha256"]!=EVIDENCE["build_candidate.py"]):
        common.fail("Changed exact frozen webseed source/build recipe")


def inspect(path,imported_library,imported_include):
    helpers=parent_helpers();common=helpers["verify_runtime_manifest.py"]
    pin=common.digest(common.regular(path))
    if pin!=MANIFEST_SHA:
        return common.inspect(path,imported_library,imported_include)
    manifest=json.loads(path.read_bytes())
    if (set(manifest)!=common.MANIFEST_KEYS or type(manifest["schema"]) is not int or manifest["schema"]!=1
            or manifest["purpose"]!="source-bound-qualification-only" or manifest["causal_qualified"] is not False
            or manifest["source_build_record"]!="provider-webseed-source-build-record.json"
            or manifest["source_build_record_sha256"]!=RECORD_SHA):
        common.fail("Wrong exact webseed manifest schema/scope")
    record_path=common.regular(path.parent/"provider-webseed-source-build-record.json")
    parent_path=common.regular(path.parent/"provider-next-source-build-record.json")
    if common.digest(record_path)!=RECORD_SHA or common.digest(parent_path)!=PARENT_SHA:
        common.fail("Changed webseed or exact b0e parent record")
    record,parent=[json.loads(p.read_bytes()) for p in (record_path,parent_path)]
    if (set(record)!=set(RECORD_KEYS) or type(record["schema"]) is not int or record["schema"]!=1
            or record["status"]!="BUILT_AND_DIRECTLY_TESTED_CANDIDATE_NOT_APPROVED"
            or record["causal_qualified"] is not False or record["native_runtime_approved"] is not False
            or record["parent_record_sha256"]!=PARENT_SHA
            or record["upstream_commit"]!="740a0b9aeabe00e762cc0efe4a0f27593db2550b"):
        common.fail("Wrong exact webseed source/build cohort")
    # Preserve full old admission, not only a copied parent JSON or release ID.
    old_manifest=path.parent/"provider-next-runtime.json"
    old=json.loads(common.regular(old_manifest).read_bytes())
    common.inspect(old_manifest,Path(old["artifact"]),Path(old["install_root"])/"include")
    roots={key:common.canonical_directory(manifest[key]) for key in ("source_root","build_root","install_root")}
    candidate=Path(CANDIDATE)
    if (any(record[key]!=str(value) for key,value in roots.items())
            or tuple(roots.values())!=tuple(candidate/name for name in ("source","build","install"))):
        common.fail("Wrong exact owned webseed source/build/install roots")
    source,build,install=roots.values()
    verify_maps(record,source,build,install,parent,common,helpers)
    artifact=common.regular(Path(manifest["artifact"]))
    if (artifact!=artifact.resolve(strict=True) or artifact!=install/"lib/libtorrent-rasterbar.so.2.0.12"
            or str(artifact)!=record["artifact"] or manifest["artifact_sha256"]!=ARTIFACT_SHA
            or record["artifact_sha256"]!=ARTIFACT_SHA or type(manifest["artifact_bytes"]) is not int
            or manifest["artifact_bytes"]!=ARTIFACT_BYTES or record["artifact_bytes"]!=ARTIFACT_BYTES
            or artifact.stat().st_size!=ARTIFACT_BYTES or common.digest(artifact)!=ARTIFACT_SHA):
        common.fail("Changed full webseed artifact")
    if imported_library.resolve(strict=True)!=artifact or imported_include.resolve(strict=True)!=install/"include":
        common.fail("Imported LT library/SDK differs from the exact webseed provider")
    verify_evidence(record,candidate,common)
    return manifest,pin


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest",required=True,type=Path)
    p.add_argument("--library",required=True,type=Path)
    p.add_argument("--include",required=True,dest="include_root",type=Path)
    p.add_argument("--header",required=True,type=Path)
    p.add_argument("--receipt",required=True,type=Path)
    args=p.parse_args()
    parent=parent_helpers()["verify_runtime_manifest.py"]
    if parent.digest(parent.regular(args.manifest))!=MANIFEST_SHA:
        parent.main();return
    manifest,pin=inspect(args.manifest,args.library,args.include_root)
    common=parent_helpers()["verify_runtime_manifest.py"]
    header=("#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n"
        "namespace biglybtc::engine::detail::runtime_binding {\n"
        f"inline constexpr char artifact[] = {common.cpp(manifest['artifact'])};\n"
        f"inline constexpr char sha256[] = {common.cpp(manifest['artifact_sha256'])};\n"
        f"inline constexpr char manifest[] = {common.cpp(pin)};\n"
        f"inline constexpr unsigned long long bytes = {ARTIFACT_BYTES}ULL;\n"+"}\n")
    receipt={"manifest_sha256":pin,"artifact_sha256":ARTIFACT_SHA,"artifact":manifest["artifact"],
        "source_files":451,"full_source_files":1074,"build_rules":13,"bundled_inputs":16,
        "installed_files":285,"sdk_headers":276,"build_files":4,"causal_qualified":False,
        "purpose":"source-bound-qualification-only","provider_webseed_candidate":True}
    # No output before ALL source, artifact, SDK, parent and actual evidence gates.
    args.header.write_text(header);args.receipt.write_text(json.dumps(receipt,indent=2)+"\n")


if __name__=="__main__":main()
