#!/usr/bin/env python3
"""Compose and adopt one approved receive capsule; no execution before root approval."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def write(p,d):p.write_text(json.dumps(d,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
approval=load(H/'approved-inputs.json');cap=pathlib.Path(approval['capsule']['path']);review=pathlib.Path(approval['review']['path'])
assert sha(cap)==approval['capsule']['sha256'] and sha(review)==approval['review']['sha256']
assert not (H/'root-adoption.json').exists() and git('rev-parse','HEAD')==approval['native_parent_commit'] and not git('status','--porcelain')
m=load(cap);W=cap.parent;rows={}
for name,row in m['source_sha256'].items():
 dest=N/name;src=W/'staging'/name;assert (sha(dest) if dest.exists() else None)==row['before'] and sha(src)==row['after_sha256']
 rows[name]={'before_path':str(W/'before'/name) if row['before'] else None,'before_sha256':row['before'],'after_path':str(src),'after_sha256':row['after_sha256']}
for name,pin in m['artifacts_sha256'].items():assert sha(W/name)==pin
for name,pin in m['composition_input_sha256'].items():assert sha(N/name)==pin
fragment=(W/'CMake.fragment').read_text();mark='# Tests section; existing outbound/channel/LS2 fixtures and targets untouched.';assert fragment.count(mark)==1
services,tests=fragment.split(mark,1)
for name,extra in [('preview/native/services/CMakeLists.txt',services),('preview/native/tests/CMakeLists.txt',mark+tests)]:
 dest=N/name;before=H/'before'/name;after=H/'composed'/name;before.parent.mkdir(parents=True,exist_ok=True);after.parent.mkdir(parents=True,exist_ok=True)
 before.write_bytes(dest.read_bytes());after.write_text(before.read_text().rstrip()+'\n\n'+extra.strip()+'\n')
 rows[name]={'before_path':str(before),'before_sha256':sha(before),'after_path':str(after),'after_sha256':sha(after)}
assert len(rows)==9
for name,row in rows.items():(N/name).parent.mkdir(parents=True,exist_ok=True);(N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes())
subprocess.run(['git','-C',str(N),'diff','--check'],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['git','-C',str(N),'add','--',*rows],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Receive bounded I2P datagrams in legacy and fast modes'],check=True)
write(H/'root-adoption.json',{'source_commit':git('rev-parse','HEAD'),'parent_commit':approval['native_parent_commit'],'source_path_count':len(rows),'source_paths':rows,'source_capsules':[approval['capsule']],'source_reviews':[approval['review']],'adopter_sha256':sha(__file__),'approved_inputs_sha256':sha(H/'approved-inputs.json')})
print('ADOPTED',git('rev-parse','HEAD'),'paths',len(rows))
