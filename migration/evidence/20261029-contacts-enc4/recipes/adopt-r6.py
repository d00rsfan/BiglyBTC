"""Root-only reviewed source amendments; batch fixes before one fresh cohort."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption-r5.json');approved=load(H/'approved-amendments-r6.json')
assert not (H/'root-adoption-r6.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='e9f90a8e89b268a6cc9246c3e8c400fc6c07e09a' and not git('status','--porcelain')
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
changes={};caps=[];reviews=[]
for capsule,review in approved['amendments']:
 assert sha(capsule['path'])==capsule['sha256'] and sha(review['path'])==review['sha256']
 p=pathlib.Path(capsule['path']);m=load(p)
 for name,pin in m['artifacts_sha256'].items():assert sha(p.parent/name)==pin
 for name,row in m['source_paths'].items():
  assert name.startswith('preview/native/') and '..' not in pathlib.PurePosixPath(name).parts and name not in changes
  assert sha(N/name)==sha(row['before_path'])==row['before_sha256'] and sha(row['after_path'])==row['after_sha256']
  if name not in a['source_paths']:
   assert hashlib.sha256(subprocess.check_output(['git','-C',str(N),'show',a['parent_commit']+':'+name])).hexdigest()==row['before_sha256'];a['source_paths'][name]=dict(row)
  changes[name]=row
 caps.append(capsule);reviews.append(review)
failed=load(H/'runs/clang-r5/result.json');assert failed['status']=='FAIL' and failed['source_commit']==a['source_commit']
assert [r['exit'] for r in load(H/'runs/clang-r5/commands.json')]==[0,0,0,8]
for name,row in changes.items():
 (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*changes],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Bound individual refresh RPCs within their overall lookup deadline'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),source_path_count=len(a['source_paths']),previous_adoption={'path':str(H/'root-adoption-r5.json'),'sha256':sha(H/'root-adoption-r5.json')},correction_capsules=a['correction_capsules']+caps,correction_reviews=a['correction_reviews']+reviews,adopter_r6_sha256=sha(__file__),continue_after_test_failures=True,approved_amendments_r6_sha256=sha(H/'approved-amendments-r6.json'),failed_fifth_clang_sha256={str(p.relative_to(H)):sha(p) for p in (H/'runs/clang-r5').iterdir() if p.is_file()})
(H/'root-adoption-r6.json').write_text(json.dumps(a,indent=2)+'\n')
(H/'run-r6.py').write_text((H/'run-r5.py').read_text().replace("H/'root-adoption-r5.json'","H/'root-adoption-r6.json'").replace("H/'jobs-r5.mk'","H/'jobs-r6.mk'").replace(",'--stop-on-failure'", ""))
(H/'jobs-r6.mk').write_text((H/'jobs-r5.mk').read_text().replace('/run-r5.py','/run-r6.py').replace('ATTEMPT ?= r5','ATTEMPT ?= r6'))
print('ADOPTED_REVIEWED_BATCH',a['source_commit'],a['source_path_count'],len(changes))
