#!/usr/bin/env python3
import argparse,hashlib,json,subprocess
from pathlib import Path
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');base='a965fd3cf0547e041d58512c56fea1cccf972ccb'
p=argparse.ArgumentParser();p.add_argument('--apply',action='store_true');a=p.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=n,text=True).strip()==base
assert subprocess.check_output(['git','branch','--show-current'],cwd=n,text=True).strip()=='main'
assert not subprocess.check_output(['git','status','--porcelain'],cwd=n,text=True).strip()
files={};capsules={}
for name in ['creator','ui','reviews/scheduler-rebind']:
 root=w/name;mp=root/'manifest.json';m=json.loads(mp.read_text());capsules[name]=sha(mp)
 changes=m.get('changes',m.get('sources'))
 for dest,row in changes.items():
  src=root/row.get('source','staging/'+dest);assert sha(src)==row['after_sha256'],str(src)
  if row.get('before_sha256') is None:assert not (n/dest).exists(),dest
  else:assert sha(n/dest)==row['before_sha256'],dest
  assert dest not in files;files[dest]=src
# Storage agent maps staging/adapters and staging/tests under preview/native.
sr=w/'storage';sm=sr/'manifest.json';assert sm.exists(),'storage freeze required';capsules['storage']=sha(sm);storage_manifest=json.loads(sm.read_text())
for src in (sr/'staging').rglob('*'):
 if src.is_file():
  dest='preview/native/'+str(src.relative_to(sr/'staging'));assert sha(src)==storage_manifest['source_sha256'][dest],dest;assert dest not in files;assert not (n/dest).exists(),dest;files[dest]=src
for src in (w/'root/staging').rglob('*'):
 if src.is_file():
  dest=str(src.relative_to(w/'root/staging'));assert dest not in files;files[dest]=src
receipt={'baseline':base,'capsules':capsules,'files':{}}
for dest,src in sorted(files.items()):
 assert dest.startswith('preview/native/') and '..' not in Path(dest).parts
 out=n/dest;old=sha(out) if out.exists() else None
 if old:
  git=subprocess.check_output(['git','show',base+':'+dest],cwd=n);assert hashlib.sha256(git).hexdigest()==old,dest
 receipt['files'][dest]={'source':str(src),'before_sha256':old,'after_sha256':sha(src)}
(w/'adoption-plan.json').write_text(json.dumps(receipt,indent=2)+'\n')
if a.apply:
 for dest,src in sorted(files.items()):
  out=n/dest;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(src.read_bytes());assert sha(out)==receipt['files'][dest]['after_sha256']
 (w/'adoption-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(('Adopted ' if a.apply else 'Reviewed ')+str(len(files))+' native source files on main; logs in '+str(w))
