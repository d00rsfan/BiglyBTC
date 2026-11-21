#!/usr/bin/env python3
"""Closed same-image shared SDK role qualification;no private rate admission."""
import argparse,hashlib,importlib.util,json,re,sys
from pathlib import Path
import xml.etree.ElementTree as ET

MANIFEST='8aa1186254a94da88de30fe002e58928b1d5ed52a179f46046047090456d7b83'
APPROVAL='402cd4f36616714a08136eacce3eeb84fca57c5e01d4476ccd5568572e083907'
PROOF='c7e2de564801a35ca92e77d49fd349a768d575333fcf1bfc80d537711bd76315'
REVIEW='1c9ab504e868ee5de8cce557af8dc4b50e800d11e016b0d7b820bd970ae143ff'
PUBLIC='f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081'
ROLES=['Public','DedicatedPrivateI2pV1','PrivateI2pMulti','PublicPrivateCoexistence','MixedI2pTransportV1']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def regular(value):
 p=Path(value);assert p.is_absolute() and p.resolve(strict=True)==p and p.is_file() and not p.is_symlink(),'Noncanonical role input'
 return p
def inspect(args):
 if not __debug__:raise RuntimeError('SDK role verification requires enabled assertions')
 manifest=regular(args.manifest);approval=regular(args.approval)
 assert sha(manifest)==MANIFEST,'Unknown all-role SDK manifest'
 assert args.approval_sha256==sha(approval)==APPROVAL,'Unknown all-role SDK approval'
 m=json.loads(manifest.read_bytes());a=json.loads(approval.read_bytes())
 assert m['schema']=='BIGLYBTC_SHARED_QUOTA_ALL_ROLES_RUNTIME_V1' and m['roles']==ROLES
 assert a['schema']=='BIGLYBTC_SHARED_QUOTA_ALL_ROLES_APPROVAL_V1' and a['roles']==ROLES and a['status']=='EXACT_SDK_ROLES_NATIVE_QUALIFICATION_ONLY'
 assert regular(a['runtime_manifest'])==manifest and a['runtime_manifest_sha256']==MANIFEST
 assert m['private_tag_payload_qualified'] is False and a['private_tag_payload_qualified'] is False and a['shipping_approved'] is False
 parent=regular(m['parent_public_manifest']);assert sha(parent)==m['parent_public_manifest_sha256']==PUBLIC
 p=json.loads(parent.read_bytes())
 for key in ['artifact','artifact_sha256','artifact_bytes','source','source_inputs_sha256','source_inventory_sha256','include','headers_sha256','sdk_tree_sha256','class_abi','shared_quota_abi','required_exports','allocator_binding']:
  assert m[key]==p[key],'Changed Public SDK cohort: '+key
 for key in ['artifact_sha256','artifact_bytes','headers_sha256','sdk_tree_sha256']:
  assert a[key]==m[key]
 assert m['source_capsule_kind']=='complete_candidate_source_inventory' and a['source_capsule_sha256']==m['source_capsule_sha256']==m['source_inventory_sha256']
 proof=regular(m['sdk_role_proof']);assert sha(proof)==m['sdk_role_proof_sha256']==a['source_build_record_sha256']==PROOF
 r=json.loads(proof.read_bytes());assert r['schema']=='BIGLYBTC_SHARED_QUOTA_SDK_ROLES_PROOF_V1' and r['status']=='SDK_ROLES_REQUALIFIED_NATIVE_PENDING' and r['roles']==ROLES
 assert r['artifact_sha256']==m['artifact_sha256'] and r['source_inventory_sha256']==m['source_inventory_sha256']
 assert r['actual_ctest_suites']==9 and r['private_owner_methods']==23 and r['common_role_quota_assertions_per_variant']==39
 assert r['private_tag_payload_qualified'] is False and r['native_private_profile_authority'] is False
 assert a['review_sha256']==REVIEW and any(h==REVIEW for h in r['inputs_sha256'].values())
 for section in ['inputs_sha256','images_sha256']:
  for path,digest in r[section].items():assert sha(regular(path))==digest,'Changed measured SDK role input: '+path
 for name,names in r['xml_cases'].items():
  rows=list(ET.parse(regular(proof.parent/name)).getroot().iter('testcase'))
  assert len(rows)==len(names) and {x.get('name') for x in rows}==set(names)
  assert all(x.get('status')=='run' and not any(x.find(k) is not None for k in ['failure','error','skipped']) for x in rows)
 # Historical private working-tree drift is recorded,not repaired or trusted.
 # Re-run the complete current Public source/header/artifact closure;new actual
 # Private/Mixed controls above provide this separately qualified role delta.
 verifier=Path(__file__).with_name('verify_runtime_manifest_public_shared_quota.py')
 spec=importlib.util.spec_from_file_location('public_shared_role_parent',verifier);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.inspect(argparse.Namespace(manifest=parent,library=args.library,include=args.include,header=args.header.with_name(args.header.name+'.public'),receipt=args.receipt.with_name(args.receipt.name+'.public')))
 return m,a
def generated(m,a):
 text='#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_CHAT_BOUND 1\n#define BIGLYBTC_PUBLIC_TAG_RATES_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_QUOTA_BOUND 1\n#define BIGLYBTC_SHARED_QUOTA_ALL_ROLES_BOUND 1\n#define BIGLYBTC_PERSISTENT_MIXED_I2P_BOUND 1\nnamespace biglybtc::engine::runtime_binding {\n'
 for key,value in {'artifact':m['artifact'],'sha256':m['artifact_sha256'],'manifest':MANIFEST,'mixed_approval_sha256':APPROVAL,'source_capsule_sha256':m['source_capsule_sha256'],'sdk_tree_sha256':m['sdk_tree_sha256'],'source_build_record_sha256':PROOF}.items():text+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 return text+'inline constexpr unsigned long long bytes = '+str(m['artifact_bytes'])+'ULL;\n}\n'
def main():
 p=argparse.ArgumentParser()
 for name in ['manifest','approval','library','include','header','receipt']:p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--approval-sha256',required=True);args=p.parse_args();m,a=inspect(args)
 args.header.write_text(generated(m,a));args.receipt.write_text(json.dumps(dict(status='EXACT_SHARED_QUOTA_ALL_SDK_ROLES',manifest_sha256=MANIFEST,approval_sha256=APPROVAL,roles=ROLES,exports=59,private_tag_payload_qualified=False),indent=2)+'\n')
if __name__=='__main__':
 try:main()
 except Exception as error:print('Shared SDK role admission rejected:',error,file=sys.stderr);sys.exit(1)
