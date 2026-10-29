#!/usr/bin/env python3
"""Adopt the two independently reviewed overlays plus native writer regression."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
approval=load(H/'approved-inputs.json');assert not (H/'root-adoption.json').exists()
assert git('rev-parse','HEAD')==approval['native_parent_commit'] and not git('status','--porcelain')
rows={};capsules=[];reviews=[];roots={};io_fragments={}
for component in ('io','tracker'):
 item=approval[component];cap=pathlib.Path(item['capsule']['path']);review=pathlib.Path(item['review']['path'])
 assert sha(cap)==item['capsule']['sha256'] and sha(review)==item['review']['sha256']
 m=load(cap);roots[component]=cap.parent;capsules.append(item['capsule']);reviews.append(item['review'])
 if component=='io':
  for ref in m['cmake_append_fragments']:
   path=pathlib.Path(ref['path']);assert sha(path)==ref['sha256'];io_fragments[path.name]=path.read_text()
 for name,pin in m.get('artifacts',m.get('artifacts_sha256',{})).items():assert sha(cap.parent/name)==pin
 for name,pin in m.get('composition_input_sha256',{}).items():assert sha(N/name)==pin
 for name,entry in m.get('source_paths',m.get('source_sha256',{})).items():
  assert name.startswith('preview/native/') and name not in rows
  if 'after_path' in entry:
   row=dict(entry);src=pathlib.Path(row['after_path'])
  else:
   src=cap.parent/'staging'/name;row={'before_path':str(cap.parent/'before'/name) if entry['before'] else None,'before_sha256':entry['before'],'after_path':str(src),'after_sha256':entry['after_sha256']}
  assert sha(src)==row['after_sha256'] and (sha(N/name) if (N/name).exists() else None)==row['before_sha256'],name
  rows[name]=row
for item in approval['source_history']:assert sha(item['path'])==item['sha256']
for name,pin in approval['extra_sources'].items():
 src=H/'staging'/name;assert not (N/name).exists() and sha(src)==pin and name not in rows
 rows[name]={'before_path':None,'before_sha256':None,'after_path':str(src),'after_sha256':pin}
fragment=(roots['tracker']/'CMake.fragment').read_text();marker='# Tests: append once; no source/compiler/provider options or old tests change.';assert fragment.count(marker)==1
services,tracker_tests=fragment.split(marker,1)
extra={'preview/native/app/CMakeLists.txt':[io_fragments['app.CMake.fragment']],
 'preview/native/services/CMakeLists.txt':[services],
 'preview/native/tests/CMakeLists.txt':[io_fragments['tests.CMake.fragment'],marker+tracker_tests,(H/'native-tests.CMake.fragment').read_text()]}
assert sha(H/'native-tests.CMake.fragment')==approval['native_tests_fragment_sha256']
for name,parts in extra.items():
 before=H/'before'/name;after=H/'composed'/name;before.parent.mkdir(parents=True,exist_ok=True);after.parent.mkdir(parents=True,exist_ok=True)
 before.write_bytes((N/name).read_bytes());after.write_text(before.read_text().rstrip()+'\n\n'+'\n\n'.join(p.strip() for p in parts)+'\n')
 rows[name]={'before_path':str(before),'before_sha256':sha(before),'after_path':str(after),'after_sha256':sha(after)}
assert len(rows)==31
for name,row in rows.items():
 dest=N/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(pathlib.Path(row['after_path']).read_bytes())
subprocess.run(['git','-C',str(N),'diff','--check'],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['git','-C',str(N),'add','--',*rows],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Offload DHT profile I/O and add explicit I2P tracker scrape'],check=True)
d={'source_commit':git('rev-parse','HEAD'),'parent_commit':approval['native_parent_commit'],'source_path_count':len(rows),'source_paths':rows,'source_capsules':capsules,'source_reviews':reviews,'source_history':approval['source_history'],'adopter_sha256':sha(__file__),'approved_inputs_sha256':sha(H/'approved-inputs.json')}
(H/'root-adoption.json').write_text(json.dumps(d,indent=2)+'\n');print('ADOPTED_COMBINED',d['source_commit'],'paths',len(rows))
