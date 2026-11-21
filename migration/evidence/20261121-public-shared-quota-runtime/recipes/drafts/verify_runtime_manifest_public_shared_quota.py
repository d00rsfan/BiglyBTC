#!/usr/bin/env python3
"""Exact Public shared-quota SDK admission;private/mixed roles remain denied."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

MANIFEST='f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081'
PARENT='774c4f7521a7e01fae96a3cc44f73ebf08c35fd6584114f1581d6878f6fecddf'
ARTIFACT='05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e'
SOURCE='4859f8aadce41c54e91875d1ccda71cb52beb656267e8f3a55f91757375f5ec4'
HEADERS='c50c9e445b89d1a700ad19d8ca031cf12257913858acdb8dde5d82658e142ac2'
CHANGED={
 'CMakeLists.txt','include/libtorrent/aux_/bandwidth_limit.hpp',
 'include/libtorrent/aux_/bandwidth_manager.hpp',
 'include/libtorrent/aux_/bandwidth_queue_entry.hpp',
 'include/libtorrent/aux_/biglybtc_shared_binding.hpp',
 'include/libtorrent/aux_/biglybtc_shared_quota.hpp',
 'include/libtorrent/biglybtc_shared_quota.hpp','include/libtorrent/biglybtc_tag_gate.hpp',
 'include/libtorrent/peer_class.hpp','src/bandwidth_limit.cpp',
 'src/bandwidth_manager.cpp','src/bandwidth_queue_entry.cpp',
 'src/biglybtc_shared_binding.cpp','src/biglybtc_shared_quota.cpp',
 'src/biglybtc_tag_gate.cpp','src/biglybtc_tag_gate.hpp',
 'src/biglybtc_tag_shared.cpp','src/session_impl.cpp'}
EXPORTS={
 'biglybtc_shared_quota_features_v1','biglybtc_shared_quota_scope_create_v1',
 'biglybtc_shared_quota_scope_close_v1','biglybtc_shared_quota_scope_release_v1',
 'biglybtc_shared_quota_acquire_v1','biglybtc_shared_quota_set_v1',
 'biglybtc_tag_shared_bind_v1'}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def canonical(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def regular(value):
 p=Path(value)
 assert p.is_absolute() and p.resolve(strict=True)==p and p.is_file() and not p.is_symlink(),'Non-canonical SDK input'
 return p
def directory(value):
 p=Path(value)
 assert p.is_absolute() and p.resolve(strict=True)==p and p.is_dir() and not p.is_symlink(),'Non-canonical SDK directory'
 return p
def leaf(root,name):
 p=Path(name);assert not p.is_absolute() and '..' not in p.parts
 return regular(root/p)

def inspect(args):
 if not __debug__:raise RuntimeError("SDK verification requires enabled assertions")
 manifest=regular(args.manifest);assert sha(manifest)==MANIFEST,'Unknown shared-quota manifest'
 m=json.loads(manifest.read_bytes())
 assert m['schema']=='BIGLYBTC_PUBLIC_SHARED_QUOTA_RUNTIME_V1' and m['roles']==['Public'],'Unqualified SDK roles'
 assert m['allocator_binding']=='ONE_REVIEWED_SDK_IMAGE_ALL_ADMITTED_SESSIONS'
 assert m['class_abi']=='0x4242435300020002' and m['shared_quota_abi']=='0x4242515300010001'
 parent=regular(m['parent_record']);assert sha(parent)==m['parent_record_sha256']==PARENT
 previous=json.loads(parent.read_bytes())
 # Reuse the complete pinned ancestor checks,not a weaker declaration that the
 # current candidate merely resembles the prior accepted provider.
 verifier=regular(Path(__file__).with_name('verify_runtime_manifest_public_tag_rates.py'))
 module_spec=importlib.util.spec_from_file_location('parent_public_tag_runtime',verifier)
 module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
 module.inspect(argparse.Namespace(manifest=parent,library=Path(previous['artifact']),include=Path(previous['include']),
     header=args.header.with_name(args.header.name+'.parent'),receipt=args.receipt.with_name(args.receipt.name+'.parent')))
 source=directory(m['source']);inputs=m['source_inputs_sha256']
 assert len(inputs)==1102 and canonical(inputs)==m['source_inventory_sha256']==SOURCE
 assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(inputs)
 for name,digest in inputs.items():assert sha(leaf(source,name))==digest,'Changed SDK source: '+name
 old=previous['source_inputs_sha256']
 assert {name for name in set(old)|set(inputs) if old.get(name)!=inputs.get(name)}==CHANGED,'Unexpected SDK source delta'
 include=directory(m['include']);assert args.include.resolve(strict=True)==include
 headers=m['headers_sha256'];assert len(headers)==290 and canonical(headers)==m['sdk_tree_sha256']==HEADERS
 assert {str(p.relative_to(include)) for p in (include/'libtorrent').rglob('*') if p.is_file()}==set(headers)
 for name,digest in headers.items():assert sha(leaf(include,name))==inputs['include/'+name]==digest,'Changed SDK header: '+name
 library=regular(m['artifact']);assert args.library.resolve(strict=True)==library
 assert sha(library)==m['artifact_sha256']==ARTIFACT and library.stat().st_size==m['artifact_bytes']==10276784
 exports=m['required_exports'];assert len(exports)==len(set(exports))==59 and set(exports)==set(previous['required_exports'])|EXPORTS
 symbols=subprocess.run(['readelf','--dyn-syms','--wide',str(library)],check=True,stdout=subprocess.PIPE,text=True).stdout
 exported={line.split()[-1].split('@')[0] for line in symbols.splitlines() if len(line.split())>=8 and line.split()[6]!='UND'}
 assert set(exports)<=exported,'Missing shared-quota/parent export'
 header='#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_CHAT_BOUND 1\n#define BIGLYBTC_PUBLIC_TAG_RATES_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_QUOTA_BOUND 1\nnamespace biglybtc::engine::runtime_binding {\n'
 for key,value in {'artifact':str(library),'sha256':ARTIFACT,'manifest':MANIFEST,'source_capsule_sha256':HEADERS}.items():header+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 header+='inline constexpr unsigned long long bytes = 10276784ULL;\n}\n';args.header.write_text(header)
 args.receipt.write_text(json.dumps(dict(status='EXACT_PUBLIC_SHARED_QUOTA_SDK',manifest_sha256=MANIFEST,artifact_sha256=ARTIFACT,roles=['Public'],exports=59,source_files=1102,installed_headers=290,private_or_mixed_qualified=False),indent=2)+'\n')

if __name__=='__main__':
 p=argparse.ArgumentParser()
 for name in ['manifest','library','include','header','receipt']:p.add_argument('--'+name,type=Path,required=True)
 try:inspect(p.parse_args())
 except Exception as e:print('Public shared-quota runtime rejected:',str(e),file=sys.stderr);sys.exit(1)
