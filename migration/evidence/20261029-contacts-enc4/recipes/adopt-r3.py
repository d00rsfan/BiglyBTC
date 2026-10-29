"""Reviewed bounded backpressure correction; no test replay here."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
P=H/'amendments/normal-preflight-backpressure/manifest.json';V=R/'build/azureus-maintenance-contacts-20261029-review/normal-preflight-backpressure.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption-r2.json');assert not (H/'root-adoption-r3.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='f443b0f21975c69fbddf557618f6dd9b43bbc302' and not git('status','--porcelain')
assert sha(P)=='5d95ea1d5e2b1b9ae2ab4c68fd45d1a26ae998f61fe29da0d38f7e426f6c5c75' and sha(V)=='3cc39ec16c5b91f4f7057c76ed05cc60d36c314b3c2d1b47f85530f76c7bb0a9';m=load(P)
for name,pin in m['artifacts_sha256'].items():assert sha(P.parent/name)==pin
failed=load(H/'runs/clang-r2/result.json');assert failed['status']=='FAIL' and failed['source_commit']==a['source_commit'] and [r['exit'] for r in load(H/'runs/clang-r2/commands.json')]==[0,0,0,8]
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
assert len(m['source_paths'])==4
for name,row in m['source_paths'].items():
 assert name.startswith('preview/native/') and '..' not in pathlib.PurePosixPath(name).parts
 assert sha(N/name)==sha(row['before_path'])==row['before_sha256'] and sha(row['after_path'])==row['after_sha256']
 if name not in a['source_paths']:
  assert hashlib.sha256(subprocess.check_output(['git','-C',str(N),'show',a['parent_commit']+':'+name])).hexdigest()==row['before_sha256'];a['source_paths'][name]=dict(row)
 else:a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
for name,row in m['source_paths'].items():(N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes())
subprocess.run(['git','-C',str(N),'add','--',*m['source_paths']],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Queue normal DHT profile checks within existing bounded slots'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),source_path_count=len(a['source_paths']),previous_adoption={'path':str(H/'root-adoption-r2.json'),'sha256':sha(H/'root-adoption-r2.json')},correction_capsules=a['correction_capsules']+[{'path':str(P),'sha256':sha(P)}],correction_reviews=a['correction_reviews']+[{'path':str(V),'sha256':sha(V)}],adopter_r3_sha256=sha(__file__),failed_second_clang_sha256={str(p.relative_to(H)):sha(p) for p in (H/'runs/clang-r2').iterdir() if p.is_file()})
(H/'root-adoption-r3.json').write_text(json.dumps(a,indent=2)+'\n')
(H/'run-r3.py').write_text((H/'run-r2.py').read_text().replace("H/'root-adoption-r2.json'","H/'root-adoption-r3.json'").replace("H/'jobs-r2.mk'","H/'jobs-r3.mk'"))
(H/'jobs-r3.mk').write_text((H/'jobs-r2.mk').read_text().replace('/run-r2.py','/run-r3.py').replace('ATTEMPT ?= r2','ATTEMPT ?= r3'))
print('ADOPTED_BOUNDED_BACKPRESSURE',a['source_commit'],a['source_path_count'])
