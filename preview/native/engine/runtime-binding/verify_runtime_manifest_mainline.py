#!/usr/bin/env python3
"""Additive exact PORT source/SDK/artifact proof. Reviewed for private native qualification only."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
MANIFEST_SHA = '1267bf58a235dc6c32d9ffadab8bbd994ec81f2aa958f0cf621f8a71e4f6c84c'
RECORD_SHA = 'c848411975cecc955370cf0a95790fd4659a52714b8f57ce9c82fa7e9fbf54d3'
PARENT_HELPER_SHA = '3a6cc04acda2d6ded210fa81487f2bc3ccf50d2c8b13ad3a2ec5025f4632eded'
PARENT_RECORD_SHA = '9f8d1f436f943994a117f9f33bbaf016f92bb60ab3c0cecf9e0b6f02efb3c6ee'
CANDIDATE = '/home/yurii/Java/BiglyBT/build/mainline-port-20261028/provider-candidate'
NEW_EXPORTS = {'biglybtc_mainline_port_abi_v1','biglybtc_attach_mainline_port_observer_v1','biglybtc_refresh_mainline_port_v1'}
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def parent():
    path=Path(__file__).with_name('verify_runtime_manifest_socks.py')
    if path.is_symlink() or digest(path)!=PARENT_HELPER_SHA:raise ValueError('Changed pinned SOCKS runtime helper')
    sys.dont_write_bytecode=True
    spec=importlib.util.spec_from_file_location('biglybtc_mainline_parent',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def inspect(path,library,include):
    old=parent();helpers=old.parent().parent().parent().parent_helpers();common=helpers['verify_runtime_manifest.py']
    if common.digest(common.regular(path))!=MANIFEST_SHA:return old.inspect(path,library,include)
    manifest=json.loads(path.read_bytes());record_path=common.regular(path.parent/'provider-mainline-source-build-record.json')
    if digest(record_path)!=RECORD_SHA:common.fail('Changed PORT source/build receipt')
    record=json.loads(record_path.read_bytes());candidate=Path(CANDIDATE)
    if (set(manifest)!=common.MANIFEST_KEYS or manifest['purpose']!='source-bound-qualification-only'
        or manifest['schema']!=1 or manifest['causal_qualified'] is not False
        or manifest['source_build_record']!=record_path.name or manifest['source_build_record_sha256']!=RECORD_SHA
        or record['status']!='BUILT_AND_DIRECTLY_TESTED_CANDIDATE_NOT_APPROVED'
        or record['causal_qualified'] is not False or record['native_runtime_approved'] is not False
        or record['parent_record_sha256']!=PARENT_RECORD_SHA):common.fail('Wrong PORT qualification scope')
    previous_path=path.parent/'provider-socks-runtime.json';previous=json.loads(common.regular(previous_path).read_bytes())
    old.inspect(previous_path,Path(previous['artifact']),Path(previous['install_root'])/'include')
    previous_record=json.loads(common.regular(path.parent/'provider-socks-source-build-record.json').read_bytes())
    if set(record['required_exports'])!=set(previous_record['required_exports'])|NEW_EXPORTS or len(record['required_exports'])!=20:common.fail('Incomplete PORT required exports')
    for key in ('source_root','build_root','install_root'):
        if record[key]!=manifest[key] or common.canonical_directory(record[key])!=candidate/key.removesuffix('_root'):common.fail('Wrong PORT owned root')
    source,build,install=[Path(record[k]) for k in ('source_root','build_root','install_root')]
    for key,root,count in [('source_inputs_sha256',source,1076),('installed_sha256',install,287),('build_files_sha256',build,4)]:common.verify_map(record,key,root,count)
    common.verify_map(record,'source_sha256',source,453)
    tree=helpers['verify_provider_runtime.py'].tree_keys
    if tree(source)!=set(record['source_inputs_sha256']) or tree(install,True)!=set(record['installed_sha256']):common.fail('Changed complete PORT source/SDK inventory')
    headers={k:v for k,v in record['installed_sha256'].items() if k.startswith('include/')}
    if len(headers)!=278 or any(record['source_inputs_sha256'].get(k)!=v for k,v in headers.items()):common.fail('PORT installed SDK differs from exact source')
    artifact=common.regular(Path(manifest['artifact']))
    if (artifact!=install/'lib/libtorrent-rasterbar.so.2.0.12' or str(artifact)!=record['artifact']
        or manifest['artifact_sha256']!=record['artifact_sha256'] or digest(artifact)!=record['artifact_sha256']
        or manifest['artifact_bytes']!=record['artifact_bytes'] or artifact.stat().st_size!=record['artifact_bytes']
        or library.resolve(strict=True)!=artifact or include.resolve(strict=True)!=install/'include'):common.fail('Wrong PORT artifact or imported SDK')
    for name,pin in record['evidence_sha256'].items():
        if digest(common.regular(candidate/name))!=pin:common.fail('Changed PORT actual evidence: '+name)
    suites={'provider-tests':{'test_biglybtc_swarm','test_biglybtc_utf16_info','test_biglybtc_traffic_counters'},
        'queue-tests':{'queue-observer-v1','queue-observer-v2'},'socks4a-tests':{'socks4a-provider'},
        'traffic-tests':{'traffic-v3-owned-corrupt-duplicate'},'cancel-tests':{'socks-cancellation'},'mainline-tests':{'mainline-port-bridge'}}
    for name,expected in suites.items():
        qualification=record['direct_qualification'][name]
        xml=common.regular(candidate/qualification['xml'])
        if digest(xml)!=qualification['xml_sha256']:common.fail('Changed direct XML: '+name)
        rows=list(ET.fromstring(xml.read_bytes()).iter('testcase'))
        if len(rows)!=len(expected) or {r.get('name') for r in rows}!=expected or any(r.find(t) is not None for r in rows for t in ('failure','error','skipped')):common.fail('Missing actual PORT provider tests: '+name)
    command_path=common.regular(candidate/'effective-commands.json')
    if digest(command_path)!=record['effective_commands_sha256']:common.fail('Changed effective PORT commands')
    commands=json.loads(command_path.read_bytes())
    names=['configure','build','install','provider-tests']+[kind+'-'+step for kind in ('queue','socks4a','traffic','cancel','mainline') for step in ('configure','build','tests')]
    if [r['name'] for r in commands]!=names or any(type(r['exit']) is not int or r['exit']!=0 for r in commands):common.fail('Incomplete actual PORT provider commands')
    return manifest,MANIFEST_SHA

def main():
    old=parent();common=old.parent().parent().parent().parent_helpers()['verify_runtime_manifest.py']
    p=argparse.ArgumentParser();p.add_argument('--manifest',required=True,type=Path);p.add_argument('--library',required=True,type=Path);p.add_argument('--include',dest='include_root',required=True,type=Path);p.add_argument('--header',required=True,type=Path);p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
    if digest(common.regular(a.manifest))!=MANIFEST_SHA:old.main();return
    m,pin=inspect(a.manifest,a.library,a.include_root)
    header=('#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\nnamespace biglybtc::engine::detail::runtime_binding {\n'+''.join(f'inline constexpr char {k}[] = {common.cpp(v)};\n' for k,v in [('artifact',m['artifact']),('sha256',m['artifact_sha256']),('manifest',pin)])+f"inline constexpr unsigned long long bytes = {m['artifact_bytes']}ULL;\n"+'}\n')
    a.header.write_text(header);a.receipt.write_text(json.dumps({'manifest_sha256':pin,'artifact_sha256':m['artifact_sha256'],'artifact':m['artifact'],'provider_queue_observer':True,'provider_traffic_v3':True,'provider_socks_cancellation':True,'provider_mainline_port':True,'causal_qualified':False,'purpose':m['purpose']},indent=2)+'\n')
if __name__=='__main__':main()
