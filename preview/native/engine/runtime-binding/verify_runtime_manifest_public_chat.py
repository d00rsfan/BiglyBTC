#!/usr/bin/env python3
"""Exact derived Public-only shared-chat SDK; no private/mixed-role authority."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
MANIFEST='ee50a30e5c5a7410d1d87b31317ffdbee6d51a5f7a77026301c5046677841f20'
PARENT='e213ba3227a464fcb8eab1c7db3e3adef720f482fd352bb87ec12c3f27ec1e92'
ARTIFACT='0c627e364aa10b80a9bc83cf13b67aca5253f725afc0cb32292ef57ea4825fb7'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def regular(p):
 p=Path(p);assert p.is_absolute() and p.resolve(strict=True)==p and p.is_file() and not p.is_symlink(), 'Non-canonical SDK input';return p
def leaf(root,name):
 p=Path(name);assert not p.is_absolute() and '..' not in p.parts;return regular(root/p)
def inspect(args):
 manifest=regular(args.manifest);assert sha(manifest)==MANIFEST,'Unknown shared-chat cohort'
 m=json.loads(manifest.read_bytes());assert m['schema']=='BIGLYBTC_PUBLIC_SHARED_CHAT_RUNTIME_V1' and m['roles']==['Public']
 parent=regular(m['parent_record']);assert sha(parent)==m['parent_record_sha256']==PARENT
 previous=json.loads(parent.read_bytes());source=Path(m['source']);assert source.resolve(strict=True)==source and not source.is_symlink()
 assert len(m['source_inputs_sha256'])==1093
 assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(m['source_inputs_sha256'])
 for name,digest in m['source_inputs_sha256'].items():assert sha(leaf(source,name))==digest,'Changed SDK source: '+name
 changed={k for k,v in previous['source_inputs_sha256'].items() if m['source_inputs_sha256'].get(k)!=v}
 assert changed=={'CMakeLists.txt','src/session_impl.cpp','include/libtorrent/aux_/session_impl.hpp'}
 assert set(m['source_inputs_sha256'])-set(previous['source_inputs_sha256'])=={'include/libtorrent/biglybtc_public_udp.hpp','src/biglybtc_public_udp.cpp','test/test_biglybtc_public_udp.cpp'}
 include=Path(m['include']);assert args.include.resolve(strict=True)==include and not include.is_symlink()
 assert len(m['headers_sha256'])==286
 assert {str(p.relative_to(include)) for p in (include/'libtorrent').rglob('*') if p.is_file()}==set(m['headers_sha256'])
 for name,digest in m['headers_sha256'].items():assert sha(leaf(include,name))==m['source_inputs_sha256']['include/'+name]==digest,'Changed SDK header: '+name
 assert hashlib.sha256(json.dumps(m['headers_sha256'],sort_keys=True,separators=(',',':')).encode()).hexdigest()==m['sdk_tree_sha256']
 library=regular(m['artifact']);assert args.library.resolve(strict=True)==library
 assert sha(library)==m['artifact_sha256']==ARTIFACT and library.stat().st_size==m['artifact_bytes']==10187896
 assert len(m['required_exports'])==len(set(m['required_exports']))==47
 assert set(previous['required_exports'])<=set(m['required_exports'])
 symbols=subprocess.run(['readelf','--dyn-syms','--wide',str(library)],check=True,stdout=subprocess.PIPE,text=True).stdout
 exported={line.split()[-1].split('@')[0] for line in symbols.splitlines() if len(line.split())>=8 and line.split()[6]!='UND'}
 assert set(m['required_exports'])<=exported,'Missing parent/chat export'
 header='#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_CHAT_BOUND 1\nnamespace biglybtc::engine::runtime_binding {\n'
 for key,value in {'artifact':str(library),'sha256':ARTIFACT,'manifest':MANIFEST,'source_capsule_sha256':m['sdk_tree_sha256']}.items():header+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 header+='inline constexpr unsigned long long bytes = 10187896ULL;\n}\n';args.header.write_text(header)
 args.receipt.write_text(json.dumps({'status':'EXACT_PUBLIC_SHARED_CHAT_SDK','manifest_sha256':MANIFEST,'artifact_sha256':ARTIFACT,'roles':['Public'],'exports':47},indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--include',type=Path,required=True);p.add_argument('--header',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);args=p.parse_args()
 try:inspect(args)
 except Exception as e:print('Public chat runtime rejected:',str(e),file=sys.stderr);sys.exit(1)
