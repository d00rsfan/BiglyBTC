#!/usr/bin/env python3
"""Exact Public tag-direction provider derived from the shared-chat SDK."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
MANIFEST='254b114ae5a38788518b2756867a0dcfc880cb234b19f8949f6fcf010ec00442'
PARENT='501575c39ebaecc246fdda0ded735dda45ad933655eaf0f28269295f8fe1e14f'
ARTIFACT='603f789999b5ff13f35f2be6bb955db09f48adac9d7b2e5a8bf72a8a751d974c'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def regular(p):
 p=Path(p);assert p.is_absolute() and p.resolve(strict=True)==p and p.is_file() and not p.is_symlink(), 'Non-canonical SDK input';return p
def leaf(root,name):
 p=Path(name);assert not p.is_absolute() and '..' not in p.parts;return regular(root/p)
def inspect(args):
 manifest=regular(args.manifest);assert sha(manifest)==MANIFEST,'Unknown shared-chat cohort'
 m=json.loads(manifest.read_bytes());assert m['schema']=='BIGLYBTC_PUBLIC_TAG_RATES_RUNTIME_V1' and m['roles']==['Public']
 parent=regular(m['parent_record']);assert sha(parent)==m['parent_record_sha256']==PARENT
 previous=json.loads(parent.read_bytes());source=Path(m['source']);assert source.resolve(strict=True)==source and not source.is_symlink()
 assert len(m['source_inputs_sha256'])==1096
 assert {str(p.relative_to(source)) for p in source.rglob('*') if p.is_file()}==set(m['source_inputs_sha256'])
 for name,digest in m['source_inputs_sha256'].items():assert sha(leaf(source,name))==digest,'Changed SDK source: '+name
 changed={k for k,v in previous['source_inputs_sha256'].items() if m['source_inputs_sha256'].get(k)!=v}
 assert changed=={'CMakeLists.txt','src/peer_class.cpp','src/peer_connection.cpp','src/session_impl.cpp','include/libtorrent/aux_/bandwidth_queue_entry.hpp','src/bandwidth_queue_entry.cpp','src/bandwidth_manager.cpp'}
 assert set(m['source_inputs_sha256'])-set(previous['source_inputs_sha256'])=={'include/libtorrent/biglybtc_tag_gate.hpp','src/biglybtc_tag_gate.cpp','src/biglybtc_tag_gate.hpp'}
 include=Path(m['include']);assert args.include.resolve(strict=True)==include and not include.is_symlink()
 assert len(m['headers_sha256'])==287
 assert {str(p.relative_to(include)) for p in (include/'libtorrent').rglob('*') if p.is_file()}==set(m['headers_sha256'])
 for name,digest in m['headers_sha256'].items():assert sha(leaf(include,name))==m['source_inputs_sha256']['include/'+name]==digest,'Changed SDK header: '+name
 assert hashlib.sha256(json.dumps(m['headers_sha256'],sort_keys=True,separators=(',',':')).encode()).hexdigest()==m['sdk_tree_sha256']
 library=regular(m['artifact']);assert args.library.resolve(strict=True)==library
 assert sha(library)==m['artifact_sha256']==ARTIFACT and library.stat().st_size==m['artifact_bytes']==10207456
 assert len(m['required_exports'])==len(set(m['required_exports']))==51
 assert set(previous['required_exports'])<=set(m['required_exports'])
 symbols=subprocess.run(['readelf','--dyn-syms','--wide',str(library)],check=True,stdout=subprocess.PIPE,text=True).stdout
 exported={line.split()[-1].split('@')[0] for line in symbols.splitlines() if len(line.split())>=8 and line.split()[6]!='UND'}
 assert set(m['required_exports'])<=exported,'Missing parent/chat export'
 header='#pragma once\n#define BIGLYBTC_PERSISTENT_RUNTIME_BOUND 1\n#define BIGLYBTC_PUBLIC_SHARED_CHAT_BOUND 1\n#define BIGLYBTC_PUBLIC_TAG_RATES_BOUND 1\nnamespace biglybtc::engine::runtime_binding {\n'
 for key,value in {'artifact':str(library),'sha256':ARTIFACT,'manifest':MANIFEST,'source_capsule_sha256':m['sdk_tree_sha256']}.items():header+='inline constexpr char '+key+'[] = '+json.dumps(value)+';\n'
 header+='inline constexpr unsigned long long bytes = 10207456ULL;\n}\n';args.header.write_text(header)
 args.receipt.write_text(json.dumps({'status':'EXACT_PUBLIC_TAG_RATES_SDK','manifest_sha256':MANIFEST,'artifact_sha256':ARTIFACT,'roles':['Public'],'exports':51},indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--include',type=Path,required=True);p.add_argument('--header',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);args=p.parse_args()
 try:inspect(args)
 except Exception as e:print('Public tag-rate runtime rejected:',str(e),file=sys.stderr);sys.exit(1)
