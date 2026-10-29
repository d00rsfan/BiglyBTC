#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git',*args],cwd=N,text=True).strip()
pins=load(H/'approved-inputs.json');assert git('rev-parse','HEAD')=='da7ee87bcd459718929bed834c855ced371b1065' and not git('status','--porcelain')
combined={};capsules=[]
for item in pins['capsules']:
 path=pathlib.Path(item['path']);assert sha(path)==item['sha256'];m=load(path)
 for rel,pin in m.get('artifacts',m.get('artifacts_sha256',{})).items():assert sha(path.parent/rel)==pin,rel
 parent=path.parent
 if 'parent_manifest' in m:
  ref=m['parent_manifest'];parent_path=pathlib.Path(ref['path'] if isinstance(ref,dict) else ref);parent_pin=ref['sha256'] if isinstance(ref,dict) else m['parent_manifest_sha256']
  assert sha(parent_path)==parent_pin;parent=parent_path.parent;pm=load(parent_path)
  for rel,pin in pm.get('artifacts',pm.get('artifacts_sha256',{})).items():assert sha(parent/rel)==pin,rel
  for rel,pin in m.get('inherited_parent_artifacts_sha256',{}).items():assert sha(parent/rel)==pin,rel
 rows=m.get('source_paths')
 if rows is None:
  rows={name:{'before_path':str(parent/'before'/name) if row['before'] is not None else None,'before_sha256':row['before'],'after_path':str((path.parent/'staging'/name) if (path.parent/'staging'/name).is_file() else (parent/'staging'/name)),'after_sha256':row['after_sha256']} for name,row in m['source_sha256'].items()}
  for name,pin in m['composition_input_sha256'].items():assert sha(N/name)==pin,name
 for name,row in rows.items():
  assert name.startswith('preview/native/') and name not in combined
  current=N/name;before=row['before_sha256'];assert (not current.exists()) if before is None else (sha(current)==before),name
  assert sha(row['after_path'])==row['after_sha256'];combined[name]=row
 capsules.append(item)
for item in pins['reviews']:assert sha(item['path'])==item['sha256']
# Preserve exact current root CMake leaves; compose only the two session fragments.
fragment=pathlib.Path(pins['cmake_fragment']['path']);assert sha(fragment)==pins['cmake_fragment']['sha256'];text=fragment.read_text();split=text.index('# Append tests section once.')
for name,addition in [('preview/native/services/CMakeLists.txt',text[:split]),('preview/native/tests/CMakeLists.txt',text[split:])]:
 assert name not in combined;before=N/name;dest=H/'composed'/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text((before.read_text().rstrip()+'\n\n'+addition).rstrip()+'\n')
 saved=H/'before'/name;saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(before.read_bytes())
 combined[name]={'before_path':str(saved),'before_sha256':sha(saved),'after_path':str(dest),'after_sha256':sha(dest)}
assert len(combined)==21
for name,row in combined.items():
 destination=N/name;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(pathlib.Path(row['after_path']).read_bytes());assert sha(destination)==row['after_sha256']
subprocess.run(['git','diff','--check'],cwd=N,check=True)
subprocess.run(['git','add','--',*combined],cwd=N,check=True)
subprocess.run(['git','-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Add explicit saved ratings and lease-backed I2P datagrams'],cwd=N,check=True)
source=git('rev-parse','HEAD');assert not git('status','--porcelain')
for name,row in combined.items():assert sha(N/name)==row['after_sha256']
write(H/'root-adoption.json',{'source_commit':source,'parent_commit':'da7ee87bcd459718929bed834c855ced371b1065','source_path_count':len(combined),'source_paths':combined,'source_capsules':capsules,'source_reviews':pins['reviews'],'cmake_fragment':pins['cmake_fragment'],'adopter_sha256':sha(__file__),'approved_inputs_sha256':sha(H/'approved-inputs.json')})
print('ADOPTED',source,len(combined))
