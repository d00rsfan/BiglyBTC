"""Root-only reviewed source amendments; batch fixes before one fresh cohort."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption-r3.json');approved=load(H/'approved-amendments-r4.json')
assert not (H/'root-adoption-r4.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='f26966a0695dbe15126c3543c33b237d31789c8e' and not git('status','--porcelain')
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
changes={};caps=[];reviews=[]
for capsule,review in approved['amendments']:
 assert sha(capsule['path'])==capsule['sha256'] and sha(review['path'])==review['sha256']
 p=pathlib.Path(capsule['path']);m=load(p)
 for name,pin in m['artifacts_sha256'].items():assert sha(p.parent/name)==pin
 for name,row in m['source_paths'].items():
  assert name.startswith('preview/native/') and '..' not in pathlib.PurePosixPath(name).parts and name not in changes
  assert sha(N/name)==sha(row['before_path'])==row['before_sha256'] and sha(row['after_path'])==row['after_sha256']
  assert name in a['source_paths'];changes[name]=row
 caps.append(capsule);reviews.append(review)
failed=load(H/'runs/clang-r3/result.json');assert failed['status']=='FAIL' and failed['source_commit']==a['source_commit']
assert [r['exit'] for r in load(H/'runs/clang-r3/commands.json')]==[0,0,0,8]
supp=load(H/'maintenance-supplement-r3/result.json');assert supp['status']=='FAIL_SUPPLEMENTAL_MAINTENANCE_ONLY' and supp['source_commit']==a['source_commit'] and supp['no_build']
for name,row in changes.items():
 (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*changes],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Wake bounded profile dispatch and correct selective refresh setup'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),source_path_count=len(a['source_paths']),previous_adoption={'path':str(H/'root-adoption-r3.json'),'sha256':sha(H/'root-adoption-r3.json')},correction_capsules=a['correction_capsules']+caps,correction_reviews=a['correction_reviews']+reviews,adopter_r4_sha256=sha(__file__),approved_amendments_r4_sha256=sha(H/'approved-amendments-r4.json'),failed_third_clang_sha256={str(p.relative_to(H)):sha(p) for p in (H/'runs/clang-r3').iterdir() if p.is_file()},failed_maintenance_supplement_sha256={str(p.relative_to(H)):sha(p) for p in (H/'maintenance-supplement-r3').iterdir() if p.is_file()})
(H/'root-adoption-r4.json').write_text(json.dumps(a,indent=2)+'\n')
(H/'run-r4.py').write_text((H/'run-r3.py').read_text().replace("H/'root-adoption-r3.json'","H/'root-adoption-r4.json'").replace("H/'jobs-r3.mk'","H/'jobs-r4.mk'"))
(H/'jobs-r4.mk').write_text((H/'jobs-r3.mk').read_text().replace('/run-r3.py','/run-r4.py').replace('ATTEMPT ?= r3','ATTEMPT ?= r4'))
print('ADOPTED_REVIEWED_BATCH',a['source_commit'],a['source_path_count'],len(changes))
