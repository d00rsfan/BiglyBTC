import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
P=H/'amendments/explicit-node-position-list/manifest.json';V=P.parent/'source-review.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption-r2.json');assert not (H/'root-adoption-r3.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='b40dcb567e46f825baa6e69e09c838543b94fecd' and not git('status','--porcelain')
assert sha(P)=='974bf04246e7df682fcccee69eee80b228ae65b30973fc85fb4c216690fdef16' and sha(V)=='50207ba58c2e6c7185f416c338e61cc343337d452e9130673575c33376380a19';m=load(P)
for name,pin in m['artifacts'].items():assert sha(P.parent/name)==pin
assert load(H/'runs/clang-r2/result.json')['failure']=="AssertionError('tests')" and load(H/'actor-supplement-r1/result.json')['status']=='PASS_SUPPLEMENTAL_ACTOR_ONLY'
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
assert list(m['source_paths'])==['preview/native/tests/azureus_routing_tests.cpp']
for name,row in m['source_paths'].items():
 assert sha(N/name)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'];(N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*m['source_paths']],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Supply mandatory empty NODE positions in routing fixture'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),previous_adoption={'path':str(H/'root-adoption-r2.json'),'sha256':sha(H/'root-adoption-r2.json')},correction_capsules=a['correction_capsules']+[{'path':str(P),'sha256':sha(P)}],correction_reviews=[a['correction_review'],{'path':str(V),'sha256':sha(V)}],adopter_r3_sha256=sha(__file__),failed_second_clang_sha256={str(p.relative_to(H)):sha(p) for p in (H/'runs/clang-r2').iterdir() if p.is_file()})
(H/'root-adoption-r3.json').write_text(json.dumps(a,indent=2)+'\n')
text=(H/'run-r2.py').read_text().replace("H/'root-adoption-r2.json'","H/'root-adoption-r3.json'").replace("H/'jobs-r2.mk'","H/'jobs-r3.mk'");(H/'run-r3.py').write_text(text)
(H/'jobs-r3.mk').write_text((H/'jobs-r2.mk').read_text().replace('/run-r2.py','/run-r3.py').replace('ATTEMPT ?= r2','ATTEMPT ?= r3'))
print('ADOPTED_TEST_ONLY',a['source_commit'])
