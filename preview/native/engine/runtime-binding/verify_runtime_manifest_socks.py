#!/usr/bin/env python3
"""Exact additive SOCKS cancellation source/artifact/SDK binding; old catalog unchanged."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
MANIFEST_SHA = '2a5e56dd4987756e74871c3aad015ad93974e6d47aad8754b0ad05d09f3ca94e'
RECORD_SHA = '9f8d1f436f943994a117f9f33bbaf016f92bb60ab3c0cecf9e0b6f02efb3c6ee'
PARENT_HELPER_SHA = '8e37f7ad597ea4d8af5665c35db4466d6e08d367e96380c0c3118454c8b7834b'
CANDIDATE = '/home/yurii/Java/BiglyBT/build/network-reliability-20261028/provider-socks-lifecycle'
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def parent():
    p=Path(__file__).with_name('verify_runtime_manifest_traffic.py')
    if p.is_symlink() or digest(p)!=PARENT_HELPER_SHA: raise ValueError('Changed old runtime helper')
    sys.dont_write_bytecode=True
    spec=importlib.util.spec_from_file_location('biglybtc_socks_parent',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def inspect(path,library,include):
    old=parent();helpers=old.parent().parent().parent_helpers();common=helpers['verify_runtime_manifest.py']
    if common.digest(common.regular(path))!=MANIFEST_SHA: return old.inspect(path,library,include)
    manifest=json.loads(path.read_bytes());record_path=common.regular(path.parent/'provider-socks-source-build-record.json')
    if digest(record_path)!=RECORD_SHA: common.fail('Changed queue provider source/build receipt')
    record=json.loads(record_path.read_bytes());candidate=Path(CANDIDATE)
    if (set(manifest)!=common.MANIFEST_KEYS or manifest['purpose']!='source-bound-qualification-only'
        or manifest['schema']!=1 or manifest['causal_qualified'] is not False
        or manifest['source_build_record']!=record_path.name or manifest['source_build_record_sha256']!=RECORD_SHA
        or record['status']!='BUILT_AND_DIRECTLY_TESTED_CANDIDATE_NOT_APPROVED'
        or record['causal_qualified'] is not False or record['native_runtime_approved'] is not False): common.fail('Wrong queue qualification scope')
    old_manifest=path.parent/'provider-traffic-runtime.json';previous=json.loads(common.regular(old_manifest).read_bytes())
    old.inspect(old_manifest,Path(previous['artifact']),Path(previous['install_root'])/'include')
    for key in ('source_root','build_root','install_root'):
        if record[key]!=manifest[key] or common.canonical_directory(record[key])!=candidate/key.removesuffix('_root'): common.fail('Wrong queue owned root')
    source,build,install=[Path(record[k]) for k in ('source_root','build_root','install_root')]
    for key,root in [('source_inputs_sha256',source),('installed_sha256',install),('build_files_sha256',build)]:
        common.verify_map(record,key,root,len(record[key]))
    tree=helpers['verify_provider_runtime.py'].tree_keys
    if tree(source)!=set(record['source_inputs_sha256']) or tree(install,True)!=set(record['installed_sha256']):common.fail('Changed queue complete source/SDK inventory')
    headers={k:v for k,v in record['installed_sha256'].items() if k.startswith('include/')}
    if len(headers)!=277 or any(record['source_inputs_sha256'].get(k)!=v for k,v in headers.items()):common.fail('Queue installed SDK differs from exact source')
    artifact=common.regular(Path(manifest['artifact']))
    if (artifact!=install/'lib/libtorrent-rasterbar.so.2.0.12' or str(artifact)!=record['artifact']
        or manifest['artifact_sha256']!=record['artifact_sha256'] or digest(artifact)!=record['artifact_sha256']
        or manifest['artifact_bytes']!=record['artifact_bytes'] or artifact.stat().st_size!=record['artifact_bytes']
        or library.resolve(strict=True)!=artifact or include.resolve(strict=True)!=install/'include'):common.fail('Wrong queue artifact or imported SDK')
    for name,pin in record['evidence_sha256'].items():
        if digest(common.regular(candidate/name))!=pin:common.fail('Changed queue actual evidence: '+name)
    for name,expected in [('provider-tests',{'test_biglybtc_swarm','test_biglybtc_utf16_info','test_biglybtc_traffic_counters'}),('queue-tests',{'queue-observer-v1','queue-observer-v2'}),('socks4a-tests',{'socks4a-provider'}),('traffic-tests',{'traffic-v3-owned-corrupt-duplicate'}),('cancel-tests',{'socks-cancellation'})]:
        rows=list(ET.fromstring((candidate/record['direct_qualification'][name]['xml']).read_bytes()).iter('testcase'))
        if len(rows)!=len(expected) or {r.get('name') for r in rows}!=expected or any(r.find(t) is not None for r in rows for t in ('failure','error','skipped')):common.fail('Missing actual provider tests: '+name)
    commands=json.loads((candidate/'effective-commands.json').read_bytes())
    if [r['name'] for r in commands]!=['configure','build','install','provider-tests','queue-configure','queue-build','queue-tests','socks4a-configure','socks4a-build','socks4a-tests','traffic-configure','traffic-build','traffic-tests','cancel-configure','cancel-build','cancel-tests'] or any(type(r['exit']) is not int or r['exit']!=0 for r in commands):common.fail('Incomplete queue provider commands')
    return manifest,MANIFEST_SHA

def main():
    old=parent();common=old.parent().parent().parent_helpers()['verify_runtime_manifest.py']
    p=argparse.ArgumentParser();p.add_argument('--manifest',required=True,type=Path);p.add_argument('--library',required=True,type=Path);p.add_argument('--include',dest='include_root',required=True,type=Path);p.add_argument('--header',required=True,type=Path);p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
    if digest(common.regular(a.manifest))!=MANIFEST_SHA:old.main();return
    m,pin=inspect(a.manifest,a.library,a.include_root)
    header=('#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\nnamespace biglybtc::engine::detail::runtime_binding {\n'+''.join(f'inline constexpr char {k}[] = {common.cpp(v)};\n' for k,v in [('artifact',m['artifact']),('sha256',m['artifact_sha256']),('manifest',pin)])+f"inline constexpr unsigned long long bytes = {m['artifact_bytes']}ULL;\n"+'}\n')
    a.header.write_text(header);a.receipt.write_text(json.dumps({'manifest_sha256':pin,'artifact_sha256':m['artifact_sha256'],'artifact':m['artifact'],'provider_queue_observer':True,'provider_traffic_v3':True,'provider_socks_cancellation':True,'causal_qualified':False,'purpose':m['purpose']},indent=2)+'\n')
if __name__=='__main__':main()
