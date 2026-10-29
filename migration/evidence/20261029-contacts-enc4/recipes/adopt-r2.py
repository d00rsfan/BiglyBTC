"""Adopt one reviewed compiler-only correction after all root jobs join."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
P=H/'amendments/mutable-contact-work/manifest.json';V=R/'build/azureus-maintenance-contacts-20261029-review/mutable-contact-work.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption.json');assert not (H/'root-adoption-r2.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='2fe669f688b4e0b64ba7205a64c4e969ca828281' and not git('status','--porcelain')
assert sha(P)=='4cff5dd23d0cd5e59a599d700e910082dd76296f4da7e6439ff1df1ce4460cde' and sha(V)=='9f508e0b3f2947f8518aba0feb1aba79193654d94552616c86b49f7b10a1de8e';m=load(P)
for name,pin in m['artifacts'].items():assert sha(P.parent/name)==pin
failed=load(H/'runs/clang-r1/result.json');assert failed['status']=='FAIL' and failed['source_commit']==a['source_commit'] and [r['exit'] for r in load(H/'runs/clang-r1/commands.json')]==[0,1]
assert load(H/'enc4-supplement-r1/result.json')['status']=='PASS_SUPPLEMENTAL_NEW_ENC4_ONLY'
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
assert [r['path'] for r in m['files']]==['preview/native/app/azureus_profile_io_owner.inc']
for row in m['files']:
 name=row['path'];assert sha(N/name)==sha(row['before'])==row['before_sha256'] and sha(row['after'])==row['after_sha256'];(N/name).write_bytes(pathlib.Path(row['after']).read_bytes());a['source_paths'][name].update(after_path=row['after'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*[r['path'] for r in m['files']]],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Allow moved contact work to run in the profile worker'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),previous_adoption={'path':str(H/'root-adoption.json'),'sha256':sha(H/'root-adoption.json')},correction_capsules=[{'path':str(P),'sha256':sha(P)}],correction_reviews=[{'path':str(V),'sha256':sha(V)}],adopter_r2_sha256=sha(__file__),failed_first_clang_sha256={str(p.relative_to(H)):sha(p) for root in (H/'runs/clang-r1',H/'enc4-supplement-r1') for p in root.iterdir() if p.is_file()})
(H/'root-adoption-r2.json').write_text(json.dumps(a,indent=2)+'\n')
(H/'run-r2.py').write_text((H/'run.py').read_text().replace("H/'root-adoption.json'","H/'root-adoption-r2.json'").replace("H/'jobs.mk'","H/'jobs-r2.mk'"))
(H/'jobs-r2.mk').write_text((H/'jobs.mk').read_text().replace('/run.py','/run-r2.py').replace('ATTEMPT ?= r1','ATTEMPT ?= r2'))
print('ADOPTED_COMPILER_ONLY',a['source_commit'])
