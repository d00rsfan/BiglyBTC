from pathlib import Path
import hashlib,json,subprocess,ctypes,os,datetime
R=Path('/home/yurii/Java/BiglyBT');T=R/'build/i2p-multi-20261030/provider';C=T/'candidate-r1';record=C/'source-build-record.json'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();m=json.loads(record.read_text());assert sha(record)=='449aa82f665786ce63f4e1778cee23bf07ca45439b762cf283b67efd0d48af0f'
for root,field in [(m['source_root'],'source_inputs_sha256'),(m['install_root'],'installed_sha256'),(str(Path(m['install_root'])/'include'),'headers_sha256'),(str(C),'evidence_sha256')]:
 for leaf,value in m[field].items():
  assert not Path(leaf).is_absolute() and '..' not in Path(leaf).parts,leaf
  assert sha(Path(root)/leaf)==value,(field,leaf)
headers={str(p.relative_to(Path(m['install_root'])/'include')):sha(p) for p in sorted((Path(m['install_root'])/'include/libtorrent').rglob('*')) if p.is_file()}
assert headers==m['headers_sha256'] and len(headers)==284
assert hashlib.sha256(json.dumps(headers,sort_keys=True,separators=(',',':')).encode()).hexdigest()==m['sdk_tree_sha256']
for name,value in m['reviewed_inputs_sha256'].items():assert sha(name)==value,name
capsule=T/'amendments/tracker-result-admission/manifest.json';assert sha(capsule)==m['source_capsule_sha256'];c=json.loads(capsule.read_text())
for leaf,v in c['source_paths'].items():assert sha(v['after_path'])==v['after_sha256']==sha(Path(m['source_root'])/leaf),leaf
artifact=Path(m['artifact']);assert artifact.stat().st_size==m['artifact_bytes'] and sha(artifact)==m['artifact_sha256'];assert len(m['required_exports'])==34
exports=subprocess.check_output(['nm','-D','--defined-only',str(artifact)],text=True);names={line.split()[-1] for line in exports.splitlines() if line.split()}
assert set(m['required_exports'])<=names
assert m['actual_controls']['actual_ctest_suites']==9 and m['actual_controls']['private_owner_methods']==10
assert not os.getenv('LD_PRELOAD') and not os.getenv('LD_AUDIT')
lib=ctypes.CDLL(str(artifact),mode=os.RTLD_NOW|os.RTLD_LOCAL)
class DlInfo(ctypes.Structure):_fields_=[('name',ctypes.c_char_p),('base',ctypes.c_void_p),('symbol',ctypes.c_char_p),('address',ctypes.c_void_p)]
dl=ctypes.CDLL('libdl.so.2');dl.dladdr.argtypes=[ctypes.c_void_p,ctypes.POINTER(DlInfo)];dl.dladdr.restype=ctypes.c_int
symbols={};base=None
for name in m['required_exports']:
 f=getattr(lib,name);info=DlInfo();assert dl.dladdr(ctypes.cast(f,ctypes.c_void_p),ctypes.byref(info));assert Path(os.fsdecode(info.name)).resolve()==artifact.resolve()
 if base is None:base=info.base
 assert info.base==base;symbols[name]=int(ctypes.cast(f,ctypes.c_void_p).value)-int(base)
assert lib.biglybtc_private_i2p_torrents_abi_v1()==0x42495401 and lib.biglybtc_private_i2p_owner_abi_v1()==0x42494f01
runtime={'schema':1,'source_capsule_sha256':m['source_capsule_sha256'],'artifact_sha256':m['artifact_sha256'],'artifact_bytes':m['artifact_bytes'],'sdk_tree_sha256':m['sdk_tree_sha256'],'required_exports':symbols,'scope':'optional private multi-torrent provider only; no Public runtime grant'}
(T/'runtime-manifest.json').write_text(json.dumps(runtime,indent=2)+'\n')
review={'schema':1,'status':'PASS_PRIVATE_ARTIFACT_CLOSURE','reviewer':'root','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_build_record_sha256':sha(record),'source_capsule_sha256':sha(capsule),'prebuild_source_review_sha256':sha(T/'root-source-review.json'),'source_files':len(m['source_inputs_sha256']),'installed_files':len(m['installed_sha256']),'headers':len(headers),'required_same_image_exports':len(symbols),'runtime_manifest_sha256':sha(T/'runtime-manifest.json'),'fresh_suites':9,'private_methods':10,'notes':['109 byte/dependency/toolchain-proven parent library objects reused;58 affected units compiled in first candidate; result-entry amendment rebuilt torrent.cpp/private test only','R1 socket-EPERM failure preserved separately; test credit is qualified escalated R2 only','Shared SAM lookup framing may finish an already-issued command, but retired-target results/effects are suppressed','Private runtime/actual application still requires separate native and isolated lifecycle qualification'],'default_public_runtime_approved':False,'shipping_approved':False}
(T/'root-artifact-review.json').write_text(json.dumps(review,indent=2)+'\n')
approval={'schema':'BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1','source_capsule_sha256':m['source_capsule_sha256'],'artifact_sha256':m['artifact_sha256'],'artifact_bytes':m['artifact_bytes'],'sdk_tree_sha256':m['sdk_tree_sha256'],'runtime_manifest_sha256':sha(T/'runtime-manifest.json'),'source_build_record_sha256':sha(record),'review_sha256':sha(T/'root-artifact-review.json'),'headers_sha256':headers}
p=T/'root-private-owner-approval.json';p.write_text(json.dumps(approval,indent=2)+'\n');print(json.dumps({'status':'PASS','approval':str(p),'approval_sha256':sha(p),'artifact_sha256':sha(artifact),'source_capsule_sha256':m['source_capsule_sha256'],'review_sha256':sha(T/'root-artifact-review.json')}))
