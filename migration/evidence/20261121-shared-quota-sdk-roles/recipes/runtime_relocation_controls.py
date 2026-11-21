"""All-role actual loaded-image controls on disposable copies only."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
D=Path(__file__).resolve().parent;R=D.parents[1]
SDK=R/'build/tag-rate-cross-owner-20261121/sdk-install/lib/libtorrent-rasterbar.so.2.0.12'
OLD=R/'build/tag-rate-capacity-20261118/sdk-install/lib'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(SDK);assert before=='05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e';rows=[]
with tempfile.TemporaryDirectory(prefix='biglybtc-shared-role-image-') as folder:
 root=Path(folder);copy=root/SDK.name;shutil.copy2(SDK,copy)
 for name in ['libtorrent-rasterbar.so.2.0','libtorrent-rasterbar.so']:(root/name).symlink_to(copy.name)
 for name,library,refuse,extra in [('exact-copy',root,False,{}),('old-image',OLD,True,{}),('audit-environment',root,True,{'LD_AUDIT':'/nonexistent-generated-role-audit.so'}),('preload-environment',root,True,{'LD_PRELOAD':'/nonexistent-generated-role-preload.so'}),('changed-copy',root,True,{})]:
  if name=='changed-copy':
   with copy.open('ab') as stream:stream.write(b'changed-generated-test-image')
  env=dict(os.environ,LD_LIBRARY_PATH=str(library),**extra)
  result=subprocess.run([str(D/'runtime-gcc/role-runtime')]+(['refuse'] if refuse else []),env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
  assert result.returncode==0 and ('PASS_ACTUAL_ALL_ROLE_RUNTIME_REFUSAL' if refuse else 'PASS_ACTUAL_ALL_ROLE_59_RUNTIME') in result.stdout,(name,result.returncode,result.stdout)
  rows.append(dict(case=name,exit=result.returncode,expected_refusal=refuse,output=result.stdout.strip()))
assert sha(SDK)==before
(D/'runtime-relocation-receipt.json').write_text(json.dumps(dict(cases=rows,original_sdk_sha256=before,original_sdk_changed=False,temporary_copies_removed=True),indent=2)+'\n')
print('PASS_SHARED_ROLE_RELOCATION',len(rows),'actual image controls')
