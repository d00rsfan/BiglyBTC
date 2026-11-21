from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile
D=Path(__file__).resolve().parent
sdk=Path('/home/yurii/Java/BiglyBT/build/tag-rate-cross-owner-20261121/sdk-install/lib/libtorrent-rasterbar.so.2.0.12')
probe=D/'probe-gcc/shared-runtime';before=hashlib.sha256(sdk.read_bytes()).hexdigest();rows=[]
with tempfile.TemporaryDirectory(prefix='biglybtc-public-quota-relocate-') as temporary:
 root=Path(temporary);copy=root/sdk.name;shutil.copy2(sdk,copy);(root/'libtorrent-rasterbar.so.2.0').symlink_to(copy.name)
 cases=[('exact-copy',False,{}),('audit-environment',True,{'LD_AUDIT':'/nonexistent-owned-test-audit.so'}),('changed-copy',True,{})]
 for label,reject,extra in cases:
  if label=='changed-copy':
   with copy.open('ab') as stream:stream.write(b'\0')
  env=dict(os.environ);env.pop('LD_PRELOAD',None);env.pop('LD_AUDIT',None);env.update(LD_LIBRARY_PATH=str(root),**extra)
  result=subprocess.run([str(probe),*(['refuse'] if reject else [])],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=30)
  assert result.returncode==0,(label,result.returncode,result.stdout[-1200:])
  marker=b'PASS_SHARED_QUOTA_RUNTIME_REFUSAL' if reject else b'PASS_59_EXPORT_PUBLIC_SHARED_QUOTA_RUNTIME';assert marker in result.stdout,label
  rows.append(dict(case=label,exit_code=0,expected_refusal=reject,output=result.stdout.decode(errors='replace').strip()))
assert hashlib.sha256(sdk.read_bytes()).hexdigest()==before
(D/'relocation-receipt.json').write_text(json.dumps(dict(cases=rows,original_sdk_sha256=before,original_sdk_changed=False,temporary_copies_removed=True),indent=2)+'\n')
print('PASS_PUBLIC_SHARED_RELOCATION exact copy,audit denial,changed-copy denial;SDK unchanged;temporary copies removed')
