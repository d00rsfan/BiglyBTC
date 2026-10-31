#!/usr/bin/env python3
"""Closed optional mixed39 role/source/SDK proof; never runs a target."""
import argparse, hashlib, importlib.util, json, re, sys
from pathlib import Path
import xml.etree.ElementTree as ET
# CLOSED until root reviews the actual artifact/controls and inserts exact pins.
MANIFEST_SHA='03694052165fab605b7f9a8c30ec9ddbb806b0d94a7eef642453f1341d838fe9'
APPROVAL_SHA='c47c93f8f2e90ef573d4e79ac7c8fb14589d0a3c3ce2e99db5be3f2577f41993'
SOURCE_CAPSULE='05a9fad2ad069fb0a4017e6253c91da1b56f7bec67b6fe514194007a126b0ee0'  # Source only; approval remains CLOSED.
BASIS_RECORD='449aa82f665786ce63f4e1778cee23bf07ca45439b762cf283b67efd0d48af0f'
COMMON_SHA='db8dc81c8d2baf5cf3859d5706bacbea68253866bdbfc03b19d24cdcf2a7cb6c'
ROLES=['Public','DedicatedPrivateI2pV1','PrivateI2pMulti','PublicPrivateCoexistence','MixedI2pTransportV1']
CASES=['same-image-34-exports-and-markers','distinct-roles-public-and-private-admission-denials','loopback-tracker-peer-transfer-survives-private-withdraw','private-mixed-settings-failure-is-session-local','public-join-preserves-private-registration-and-independent-join','joined-public-disk-bytes-exact']
NEW_EXPORTS=['biglybtc_mixed_i2p_transport_abi_v1','biglybtc_configure_mixed_i2p_transport_v1','biglybtc_admit_mixed_i2p_route_v1','biglybtc_revoke_mixed_i2p_route_v1','biglybtc_withdraw_mixed_i2p_transport_v1']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def common():
 p=Path(__file__).with_name('verify_runtime_manifest.py');assert not p.is_symlink() and digest(p)==COMMON_SHA
 sys.dont_write_bytecode=True;spec=importlib.util.spec_from_file_location('mixed_closed_map_reader',p);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def exact_xml(path,names):
 root=ET.parse(path).getroot();rows=list(root.iter('testcase'))
 assert len(rows)==len(names) and {r.get('name') for r in rows}==set(names)
 assert all(r.get('status')=='run' and not any(r.find(k) is not None for k in ('failure','error','skipped')) for r in rows)
 return rows
def inspect(args):
 assert all(re.fullmatch('[0-9a-f]{64}',v) for v in (MANIFEST_SHA,APPROVAL_SHA,SOURCE_CAPSULE)), 'Mixed cohort is not root approved'
 c=common();manifest=c.regular(args.manifest);approval_path=c.regular(args.approval)
 assert manifest.is_absolute() and approval_path.is_absolute() and digest(manifest)==MANIFEST_SHA
 assert args.approval_sha256==APPROVAL_SHA==digest(approval_path)
 m=json.loads(manifest.read_bytes());a=json.loads(approval_path.read_bytes())
 assert m['schema']==2 and m['purpose']=='one-public-handle-source-bound-key0-i2p-route' and m['roles']==ROLES
 assert a['schema']=='BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1' and a['status']=='ROOT_APPROVED_MIXED_I2P_ROLES' and a['roles']==ROLES
 assert a['runtime_manifest']==str(manifest) and a['runtime_manifest_sha256']==MANIFEST_SHA
 assert a['source_capsule_sha256']==m['source_capsule_sha256']==SOURCE_CAPSULE
 assert a['source_build_record_sha256']==m['source_build_record_sha256']
 # Genuine old approval files are rejection fixtures, never role authority.
 old=m['rejected_prior_approvals']
 assert set(old)=={'private','coexistence'}
 assert old['private']['sha256']=='f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83'
 assert old['coexistence']['sha256']=='3407d518e13a86f2a64b3d748cbe0870ef7df57712fade48cb72c87d2962ad75'
 assert all(digest(c.regular(Path(v['path'])))==v['sha256'] for v in old.values())
 assert all(re.fullmatch('[0-9a-f]{64}',a[k]) for k in ('artifact_sha256','sdk_tree_sha256','review_sha256','source_build_record_sha256'))
 record_path=c.regular(Path(m['source_build_record']));assert digest(record_path)==m['source_build_record_sha256']
 r=json.loads(record_path.read_bytes());assert r['schema']=='BIGLYBTC_MIXED_I2P_SOURCE_RECORD_V1' and r['status']=='BUILT_DIRECTLY_TESTED_NOT_APPROVED'
 assert r['source_capsule_sha256']==SOURCE_CAPSULE and r['parent_record_sha256']==BASIS_RECORD
 for key in ('artifact_sha256','artifact_bytes','sdk_tree_sha256'):
  assert a[key]==m[key]==r[key]
 assert 0<r['artifact_bytes']<=64*1024*1024
 source,install=[c.canonical_directory(r[k]) for k in ('source_root','install_root')]
 c.verify_map(r,'source_inputs_sha256',source,1090);c.verify_map(r,'installed_sha256',install,294)
 assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(r['source_inputs_sha256'])
 assert {str(p.relative_to(install)) for p in install.rglob('*') if p.is_file()}==set(r['installed_sha256'])
 headers={n.removeprefix('include/'):h for n,h in r['installed_sha256'].items() if n.startswith('include/libtorrent/')}
 assert len(headers)==285 and headers==r['headers_sha256']==a['headers_sha256']
 assert all(r['source_inputs_sha256']['include/'+n]==h for n,h in headers.items())
 assert hashlib.sha256(json.dumps(headers,sort_keys=True,separators=(',',':')).encode()).hexdigest()==r['sdk_tree_sha256']
 assert 'libtorrent/biglybtc_mixed_i2p_transport.hpp' in headers
 artifact=c.regular(Path(r['artifact']));assert artifact==install/'lib/libtorrent-rasterbar.so.2.0.12'
 assert args.library.resolve(strict=True)==artifact and args.include_root.resolve(strict=True)==install/'include'
 assert artifact.stat().st_size==r['artifact_bytes'] and digest(artifact)==r['artifact_sha256']
 assert len(r['required_exports'])==39 and len(set(r['required_exports']))==39 and r['required_exports']==m['required_exports']
 assert r['required_exports'][-5:]==NEW_EXPORTS
 for name,expected in r['evidence_sha256'].items():assert digest(c.regular(c.leaf(record_path.parent,name)))==expected,name
 controls=r['actual_controls'];assert controls['status']=='PASS_MIXED_SDK_DIRECT_CONTROLS_NOT_APPROVED'
 assert controls['actual_ctest_suites']==9 and controls['private_owner_methods']==17 and controls['fresh_separate_role_cases']==6
 assert controls['required_exports']==39 and controls['actual_roles']==CASES and controls['source_and_sdk_unchanged_during_subjects'] and controls['images_unchanged']
 proof=m['actual_proof'];run=c.canonical_directory(proof['directory'])
 assert digest(c.regular(run/'result.json'))==proof['result_sha256'] and json.loads((run/'result.json').read_bytes())==controls
 commands=c.regular(run/'commands.json');assert digest(commands)==controls['commands_sha256']==proof['commands_sha256']
 rows=json.loads(commands.read_bytes());assert rows and all(type(x['exit']) is int and x['exit']==0 and digest(c.regular(Path(x['log'])))==x['log_sha256'] for x in rows)
 assert [x['name'] for x in rows]==['configure','build','install','private-unit-list','provider-tests','test_dht-list','test_tracker-list','wire-controls','mainline-configure','mainline-build','mainline-tests','socks4a-configure','socks4a-build','socks4a-tests','roles-configure','roles-build','roles']
 exact_xml(run/'provider-tests.xml',{'test_biglybtc_private_i2p_owner','test_biglybtc_mainline_identity','test_biglybtc_swarm','test_biglybtc_tracker_response'})
 exact_xml(run/'wire-controls.xml',{'id_enforcement_v4','id_enforcement_v6','parse_i2p_peers'})
 exact_xml(run/'mainline-tests.xml',{'mainline-port-bridge'});exact_xml(run/'socks4a-tests.xml',{'socks4a-provider'});exact_xml(run/'roles-fixture/tests.xml',CASES)
 before=json.loads((run/'images-before.json').read_bytes());assert before==json.loads((run/'images-after.json').read_bytes())
 assert all(digest(c.regular(Path(p)))==h for p,h in before.items()) and before[str(artifact)]==r['artifact_sha256']
 assert json.loads((run/'source-before.json').read_bytes())==r['source_inputs_sha256']
 return m,r,a
def generated(m,r,a,approval_sha):
 text='#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PERSISTENT_MIXED_I2P_BOUND 1\n#define BIGLYBTC_PERSISTENT_COEXISTENCE_BOUND 0\nnamespace biglybtc::engine::detail::runtime_binding {\n'
 for key,value in [('artifact',r['artifact']),('sha256',r['artifact_sha256']),('manifest',MANIFEST_SHA),('mixed_approval_sha256',approval_sha),('source_capsule_sha256',SOURCE_CAPSULE),('sdk_tree_sha256',r['sdk_tree_sha256']),('source_build_record_sha256',m['source_build_record_sha256'])]:text+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 return text+'inline constexpr unsigned long long bytes = '+str(r['artifact_bytes'])+'ULL;\n}\n'
def main():
 p=argparse.ArgumentParser()
 for name in ('manifest','approval','library','header','receipt'):p.add_argument('--'+name,required=True,type=Path)
 p.add_argument('--include',dest='include_root',required=True,type=Path);p.add_argument('--approval-sha256',required=True);a=p.parse_args();m,r,approved=inspect(a)
 a.header.write_text(generated(m,r,approved,a.approval_sha256));a.receipt.write_text(json.dumps({'manifest_sha256':MANIFEST_SHA,'approval_sha256':APPROVAL_SHA,'source_build_record_sha256':m['source_build_record_sha256'],'source_capsule_sha256':SOURCE_CAPSULE,'artifact_sha256':r['artifact_sha256'],'sdk_tree_sha256':r['sdk_tree_sha256'],'same_image_exports':39,'explicit_roles':ROLES,'native_mixed_qualification_pending':True},indent=2)+'\n')
if __name__=='__main__':main()
