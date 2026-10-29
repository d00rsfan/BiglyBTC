import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
P=H/'amendments/absent-version-source/manifest.json';V=P.parent/'source-review.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption.json');assert not (H/'root-adoption-r2.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='8849b3cc2d2c13771fde3381d66beadf173fbe48' and not git('status','--porcelain')
assert sha(P)=='4ff08fd3200885cfb9c1f6b19c87af8089a26b5579e3e3d2d7382cd332e80111' and sha(V)=='76cff493c45f738e6e17c68a06c4a6a412d86a9e1f23fdf2ddddff5c3c493373';m=load(P)
for name,pin in m['artifacts'].items():assert sha(P.parent/name)==pin
assert load(H/'runs/clang-r1/result.json')['failure']=="AssertionError('tests')" and load(H/'actor-supplement-r1/result.json')['status']=='PASS_SUPPLEMENTAL_ACTOR_ONLY'
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
assert list(m['source_paths'])==['preview/native/tests/azureus_routing_tests.cpp']
for name,row in m['source_paths'].items():
 assert sha(N/name)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'];(N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*m['source_paths']],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Fix routing fixture for absent version reservations'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),initial_adoption={'path':str(H/'root-adoption.json'),'sha256':sha(H/'root-adoption.json')},correction_capsules=[{'path':str(P),'sha256':sha(P)}],correction_review={'path':str(V),'sha256':sha(V)},adopter_r2_sha256=sha(__file__),failed_first_clang_sha256={str(p.relative_to(H)):sha(p) for d in (H/'runs/clang-r1',H/'actor-supplement-r1') for p in d.iterdir() if p.is_file()})
(H/'root-adoption-r2.json').write_text(json.dumps(a,indent=2)+'\n')
text=(H/'run.py').read_text().replace("H/'root-adoption.json'","H/'root-adoption-r2.json'").replace("H/'jobs.mk'","H/'jobs-r2.mk'");(H/'run-r2.py').write_text(text)
(H/'jobs-r2.mk').write_text((H/'jobs.mk').read_text().replace('/run.py','/run-r2.py').replace('ATTEMPT ?= r1','ATTEMPT ?= r2'))
print('ADOPTED_TEST_ONLY',a['source_commit'])
