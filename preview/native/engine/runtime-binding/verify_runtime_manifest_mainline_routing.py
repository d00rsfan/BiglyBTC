#!/usr/bin/env python3
"""Additional exact reviewed socket-qualified routing provider lane; no caller approval."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

SPEC = {'candidate': '/home/yurii/Java/BiglyBT/build/mainline-dual-identity-provider-20261028/candidate-r1', 'manifest_sha256': 'bb84628c0b874c317f7bbd39dc5bd216b8ea9c19b9df5587e9773108b9e9c93f', 'record_sha256': 'ac30c3c79d71d35c85475a5dbd3d551b4f8ac2178561b45c8452e16d8994f01b', 'artifact_sha256': '35e6d55478547fdae5bce6fbc4c4c5a1bad576e2a943a1341c4df90e7f1da629', 'artifact_bytes': 9194904, 'new_exports': ['biglybtc_mainline_routing_abi_v1', 'biglybtc_query_mainline_routing_v1'], 'map_counts': {'source_inputs_sha256': 1082, 'source_sha256': 459, 'installed_sha256': 291, 'build_files_sha256': 4, 'source_build_rules_sha256': 13, 'bundled_dependency_sha256': 16}, 'header_count': 282, 'direct_cases': {'provider-tests': ['test_biglybtc_mainline_identity', 'test_biglybtc_swarm', 'test_biglybtc_tracker_response', 'test_biglybtc_traffic_counters', 'test_biglybtc_utf16_info'], 'dht-remote-tests': ['remote-bep42-v4', 'remote-bep42-v6'], 'tracker-parser-tests': ['tracker-parser-extract_peer', 'tracker-parser-extract_peer_hostname', 'tracker-parser-extract_peer_missing_ip', 'tracker-parser-extract_peer_missing_port', 'tracker-parser-extract_peer_not_a_dictionary', 'tracker-parser-parse_external_ip', 'tracker-parser-parse_external_ip6', 'tracker-parser-parse_failure_reason', 'tracker-parser-parse_hostname_peers', 'tracker-parser-parse_i2p_peers', 'tracker-parser-parse_interval', 'tracker-parser-parse_peers4', 'tracker-parser-parse_scrape_response', 'tracker-parser-parse_scrape_response_with_zero', 'tracker-parser-parse_warning'], 'queue-tests': ['queue-observer-v1', 'queue-observer-v2'], 'socks4a-tests': ['socks4a-provider'], 'traffic-tests': ['traffic-v3-owned-corrupt-duplicate'], 'cancel-tests': ['socks-cancellation'], 'mainline-tests': ['mainline-port-bridge']}, 'command_names': ['configure', 'build', 'install', 'provider-tests', 'dht-list', 'dht-remote-tests', 'tracker-list', 'tracker-parser-tests', 'queue-configure', 'queue-build', 'queue-tests', 'socks4a-configure', 'socks4a-build', 'socks4a-tests', 'traffic-configure', 'traffic-build', 'traffic-tests', 'cancel-configure', 'cancel-build', 'cancel-tests', 'mainline-configure', 'mainline-build', 'mainline-tests']}
OLD_HELPER_SHA = '015d1a5cb758c68d530f8000a7da3f23659668ac52bc98642e51d3b9838e227b'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def parent():
    path=Path(__file__).with_name('verify_runtime_manifest_original_identity.py')
    if path.is_symlink() or digest(path)!=OLD_HELPER_SHA:raise ValueError('Changed pinned existing PORT verifier')
    sys.dont_write_bytecode=True
    spec=importlib.util.spec_from_file_location('biglybtc_identity_provider_parent',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(path,library,include):
    old=parent();helpers=old.parent().parent().parent().parent().parent().parent_helpers();common=helpers['verify_runtime_manifest.py']
    if digest(common.regular(path))!=SPEC['manifest_sha256']:return old.inspect(path,library,include)
    manifest=json.loads(path.read_bytes());record_path=common.regular(path.parent/'provider-mainline-routing-source-build-record.json')
    if digest(record_path)!=SPEC['record_sha256']:common.fail('Changed exact identity/tracker source-build record')
    record=json.loads(record_path.read_bytes());candidate=Path(SPEC['candidate'])
    if (set(manifest)!=common.MANIFEST_KEYS or manifest['schema']!=1 or manifest['causal_qualified'] is not False
        or manifest['purpose']!='source-bound-qualification-only' or manifest['source_build_record']!=record_path.name
        or manifest['source_build_record_sha256']!=SPEC['record_sha256']
        or record['status']!='BUILT_AND_DIRECTLY_TESTED_CANDIDATE_NOT_APPROVED'
        or record['native_runtime_approved'] is not False or record['causal_qualified'] is not False
        or record['parent_record_sha256']!='d5fc5bc1b3188a1adda1e847df0ddeaa20e232a15536b4735a86f38175f32d20'):
        common.fail('Wrong exact optional identity/tracker scope')
    previous_path=path.parent/'provider-original-identity-runtime.json';previous=json.loads(common.regular(previous_path).read_bytes())
    old.inspect(previous_path,Path(previous['artifact']),Path(previous['install_root'])/'include')
    previous_record=json.loads(common.regular(path.parent/'provider-original-identity-source-build-record.json').read_bytes())
    if record['required_exports']!=previous_record['required_exports']+SPEC['new_exports'] or len(record['required_exports'])!=27:
        common.fail('Incomplete exact27 same-image exports')
    for key in ('source_root','build_root','install_root'):
        if record[key]!=manifest[key] or common.canonical_directory(record[key])!=candidate/key.removesuffix('_root'):
            common.fail('Wrong closed identity/tracker source/build/install root')
    source,build,install=[Path(record[k]) for k in ('source_root','build_root','install_root')]
    roots={'source_inputs_sha256':source,'source_sha256':source,'installed_sha256':install,'build_files_sha256':build,
        'source_build_rules_sha256':source,'bundled_dependency_sha256':source}
    for key,root in roots.items():common.verify_map(record,key,root,SPEC['map_counts'][key])
    tree=helpers['verify_provider_runtime.py'].tree_keys
    if tree(source)!=set(record['source_inputs_sha256']) or tree(install,True)!=set(record['installed_sha256']):
        common.fail('Changed complete identity/tracker source or installed inventory')
    headers={k:v for k,v in record['installed_sha256'].items() if k.startswith('include/')}
    if len(headers)!=SPEC['header_count'] or any(record['source_inputs_sha256'].get(k)!=v for k,v in headers.items()):
        common.fail('Installed identity/tracker SDK differs from exact source')
    artifact=common.regular(Path(manifest['artifact']))
    if (artifact!=install/'lib/libtorrent-rasterbar.so.2.0.12' or str(artifact)!=record['artifact']
        or digest(artifact)!=SPEC['artifact_sha256'] or artifact.stat().st_size!=SPEC['artifact_bytes']
        or manifest['artifact_sha256']!=record['artifact_sha256'] or manifest['artifact_sha256']!=SPEC['artifact_sha256']
        or manifest['artifact_bytes']!=record['artifact_bytes'] or manifest['artifact_bytes']!=SPEC['artifact_bytes']
        or library.resolve(strict=True)!=artifact or include.resolve(strict=True)!=install/'include'):
        common.fail('Wrong exact identity/tracker artifact or imported SDK')
    for rel,pin in record['evidence_sha256'].items():
        if digest(common.regular(candidate/rel))!=pin:common.fail('Changed actual identity/tracker evidence: '+rel)
    for name,expected in SPEC['direct_cases'].items():
        qualification=record['direct_qualification'][name];xml=common.regular(candidate/qualification['xml'])
        if digest(xml)!=qualification['xml_sha256']:common.fail('Changed actual direct XML '+name)
        cases=list(ET.fromstring(xml.read_bytes()).iter('testcase'))
        if (len(cases)!=len(expected) or {r.get('name') for r in cases}!=set(expected)
            or any(r.find(t) is not None for r in cases for t in ('failure','error','skipped'))):
            common.fail('Incomplete actual direct qualification '+name)
    commands=common.regular(candidate/'effective-commands.json')
    if digest(commands)!=record['effective_commands_sha256']:common.fail('Changed effective provider commands')
    rows=json.loads(commands.read_bytes())
    if [r['name'] for r in rows]!=SPEC['command_names'] or any(type(r['exit']) is not int or r['exit']!=0 for r in rows):
        common.fail('Incomplete actual configure/build/install/direct commands')
    return manifest,SPEC['manifest_sha256']
def main():
    old=parent();common=old.parent().parent().parent().parent().parent().parent_helpers()['verify_runtime_manifest.py']
    p=argparse.ArgumentParser();p.add_argument('--manifest',required=True,type=Path);p.add_argument('--library',required=True,type=Path)
    p.add_argument('--include',dest='include_root',required=True,type=Path);p.add_argument('--header',required=True,type=Path);p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
    if digest(common.regular(a.manifest))!=SPEC['manifest_sha256']:old.main();return
    m,pin=inspect(a.manifest,a.library,a.include_root)
    header=('#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\nnamespace biglybtc::engine::detail::runtime_binding {\n'
        +''.join(f'inline constexpr char {k}[] = {common.cpp(v)};\n' for k,v in [('artifact',m['artifact']),('sha256',m['artifact_sha256']),('manifest',pin)])
        +f"inline constexpr unsigned long long bytes = {m['artifact_bytes']}ULL;\n"+'}\n')
    receipt={'manifest_sha256':pin,'artifact_sha256':m['artifact_sha256'],'artifact':m['artifact'],
        'provider_queue_observer':True,'provider_traffic_v3':True,'provider_socks_cancellation':True,'provider_mainline_port':True,
        'provider_original_identity':True,'provider_tracker_response':True,'provider_mainline_routing':True,'causal_qualified':False,'purpose':m['purpose']}
    a.header.write_text(header);a.receipt.write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
