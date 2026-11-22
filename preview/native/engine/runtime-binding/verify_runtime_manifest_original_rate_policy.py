#!/usr/bin/env python3
"""Exact Original rate-policy SDK;retain full immutable ancestor verification."""
import argparse,hashlib,importlib.util,json,subprocess,sys
from pathlib import Path
import xml.etree.ElementTree as ET
MANIFEST='a5448526308d37a23afd18464b179e7fee5c36669f7d6353a306fc8c864e9766'
APPROVAL='321f94b21bd1e9c3d029c211d0f0196a04ed88bf98ac1b25cb8c0415c85751a8'
PROOF='b7a42e82670d1bc10a04dce7cccef1f2aa68d0847681a41d6ae296c461afc255'
REVIEW='76d9ad2ace1347a6c284ea9a7825c988261c49bc2897a723ebf50ca58731fae4'
ARTIFACT='b5d1ae92bfccdc3f655f8960465f38339da058ef6f38535a3a75fa0829308128'
SOURCE='d73432d02da0e03b0e4d5172a380051d7db8236a2d1dda10498a91d209692d37'
HEADERS='64771b21b1c459f0f90f72ca06034e10d59f7a26d04246c51d029512473de857'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def canonical(v):return hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def regular(value):
 p=Path(value);assert p.is_absolute() and p.resolve(strict=True)==p and p.is_file() and not p.is_symlink(),'Noncanonical rate-policy input';return p
def directory(value):
 p=Path(value);assert p.is_absolute() and p.resolve(strict=True)==p and p.is_dir() and not p.is_symlink(),'Noncanonical rate-policy directory';return p
def leaf(root,name):
 p=Path(name);assert not p.is_absolute() and '..' not in p.parts;return regular(root/p)
def inspect(args):
 if not __debug__:raise RuntimeError('Rate policy verification requires enabled assertions')
 manifest=regular(args.manifest);approval=regular(args.approval)
 assert sha(manifest)==MANIFEST and sha(approval)==args.approval_sha256==APPROVAL,'Unknown rate-policy qualification'
 m=json.loads(manifest.read_bytes());a=json.loads(approval.read_bytes())
 assert m['schema']=='BIGLYBTC_ORIGINAL_RATE_POLICY_RUNTIME_V1'
 assert a['schema']=='BIGLYBTC_SHARED_QUOTA_ALL_ROLES_APPROVAL_V1' and a['status']=='EXACT_SDK_ROLES_NATIVE_QUALIFICATION_ONLY'
 assert regular(a['runtime_manifest'])==manifest and a['runtime_manifest_sha256']==MANIFEST
 assert m['private_tag_payload_qualified'] is False and a['private_tag_payload_qualified'] is False and a['shipping_approved'] is False
 parent=regular(m['parent_roles_manifest']);ap=regular(m['parent_roles_approval'])
 assert sha(parent)==m['parent_roles_manifest_sha256']=='8aa1186254a94da88de30fe002e58928b1d5ed52a179f46046047090456d7b83'
 assert sha(ap)==m['parent_roles_approval_sha256']=='402cd4f36616714a08136eacce3eeb84fca57c5e01d4476ccd5568572e083907'
 previous=json.loads(parent.read_bytes())
 spec=importlib.util.spec_from_file_location('original_rate_legacy_parent',Path(__file__).with_name('verify_runtime_manifest_shared_roles_legacy.py'))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.inspect(argparse.Namespace(manifest=parent,approval=ap,approval_sha256=sha(ap),library=Path(previous['artifact']),include=Path(previous['include']),header=args.header.with_name(args.header.name+'.legacy'),receipt=args.receipt.with_name(args.receipt.name+'.legacy')))
 assert m['roles']==a['roles']==previous['roles']
 for key in ['class_abi','shared_quota_abi','allocator_binding']:assert m[key]==previous[key]
 source=directory(m['source']);inputs=m['source_inputs_sha256']
 assert len(inputs)==1102 and canonical(inputs)==m['source_inventory_sha256']==SOURCE
 assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(inputs)
 for name,digest in inputs.items():assert sha(leaf(source,name))==digest,'Changed policy SDK source: '+name
 delta=sorted(k for k in set(inputs)|set(previous['source_inputs_sha256']) if inputs.get(k)!=previous['source_inputs_sha256'].get(k))
 assert delta==m['changed_source_files'] and len(delta)==15
 include=directory(m['include']);assert args.include.resolve(strict=True)==include
 headers=m['headers_sha256'];assert len(headers)==290 and canonical(headers)==m['sdk_tree_sha256']==HEADERS
 assert {str(p.relative_to(include)) for p in (include/'libtorrent').rglob('*') if p.is_file()}==set(headers)
 for name,digest in headers.items():assert sha(leaf(include,name))==inputs['include/'+name]==digest
 library=regular(m['artifact']);assert args.library.resolve(strict=True)==library
 assert sha(library)==m['artifact_sha256']==ARTIFACT and library.stat().st_size==m['artifact_bytes']==10302752
 exports=m['required_exports'];assert len(exports)==len(set(exports))==61
 assert set(exports)==set(previous['required_exports'])|{'biglybtc_tag_lan_bypass_set_v1','biglybtc_protocol_bypass_set_v1'}
 symbols=subprocess.check_output(['readelf','--dyn-syms','--wide',str(library)],text=True)
 actual={x.split()[-1].split('@')[0] for x in symbols.splitlines() if len(x.split())>=8 and x.split()[6]!='UND'}
 assert set(exports)<=actual,'Missing qualified SDK export'
 for k in ['artifact_sha256','artifact_bytes','headers_sha256','sdk_tree_sha256']:assert a[k]==m[k]
 assert m['source_capsule_kind']=='complete_candidate_source_inventory'
 assert a['source_capsule_sha256']==m['source_capsule_sha256']==SOURCE
 proof=regular(m['sdk_role_proof']);assert sha(proof)==m['sdk_role_proof_sha256']==a['source_build_record_sha256']==PROOF
 r=json.loads(proof.read_bytes());assert r['schema']=='BIGLYBTC_ORIGINAL_RATE_POLICY_SDK_PROOF_V1' and r['status']=='SDK_ROLES_REQUALIFIED_NATIVE_PENDING'
 assert r['roles']==m['roles'] and r['artifact_sha256']==ARTIFACT and r['source_inventory_sha256']==SOURCE
 assert r['actual_ctest_suites']==9 and r['private_owner_methods']==23 and r['common_role_quota_assertions_per_variant']==39
 assert r['private_tag_payload_qualified'] is False and r['native_private_profile_authority'] is False
 assert a['review_sha256']==REVIEW and REVIEW in r['inputs_sha256'].values()
 for section in ['inputs_sha256','images_sha256']:
  for path,digest in r[section].items():assert sha(regular(path))==digest,'Changed measured SDK input: '+path
 for name,names in r['xml_cases'].items():
  rows=list(ET.parse(regular(proof.parent/name)).getroot().iter('testcase'))
  assert len(rows)==len(names) and {x.get('name') for x in rows}==set(names)
  assert all(x.get('status')=='run' and all(x.find(k) is None for k in ['failure','error','skipped']) for x in rows)
 return m,a
def generated(m,a):
 text='#pragma once\n'
 for key in ['PERSISTENT_RUNTIME','PUBLIC_SHARED_CHAT','PUBLIC_TAG_RATES','PUBLIC_SHARED_QUOTA','SHARED_QUOTA_ALL_ROLES','PERSISTENT_MIXED_I2P','ORIGINAL_RATE_POLICY']:text+='#define BIGLYBTC_'+key+'_BOUND 1\n'
 text+='namespace biglybtc::engine::runtime_binding {\n'
 for key,value in dict(artifact=m['artifact'],sha256=ARTIFACT,manifest=MANIFEST,mixed_approval_sha256=APPROVAL,source_capsule_sha256=SOURCE,sdk_tree_sha256=HEADERS,source_build_record_sha256=PROOF).items():text+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 return text+'inline constexpr unsigned long long bytes = 10302752ULL;\n}\n'
def main():
 p=argparse.ArgumentParser()
 for name in ['manifest','approval','library','include','header','receipt']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--approval-sha256',required=True);args=p.parse_args();m,a=inspect(args)
 args.header.write_text(generated(m,a));args.receipt.write_text(json.dumps(dict(status='EXACT_ORIGINAL_RATE_POLICY_SDK_ROLES',manifest_sha256=MANIFEST,approval_sha256=APPROVAL,exports=61,private_tag_payload_qualified=False),indent=2)+'\n')
if __name__=='__main__':
 try:main()
 except Exception as error:print('Original rate-policy SDK rejected:',error,file=sys.stderr);sys.exit(1)
