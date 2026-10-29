"""Root-only reviewed source amendments; batch fixes before one fresh cohort."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*a):return subprocess.check_output(['git','-C',str(N),*a],text=True).strip()
a=load(H/'root-adoption-r6.json');approved=load(H/'approved-amendments-r7.json')
assert not (H/'root-adoption-r7.json').exists() and git('rev-parse','HEAD')==a['source_commit']=='fe6cf7702b9c40f0975348721a8ab39e6df364f8' and not git('status','--porcelain')
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
failed=load(H/'runs/gcc-r6/result.json');assert failed['status']=='FAIL' and failed['source_commit']==a['source_commit']
assert [r['exit'] for r in load(H/'runs/gcc-r6/commands.json')]==[0,1]
for name,row in changes.items():
 (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'add','--',*changes],check=True);subprocess.run(['git','-C',str(N),'diff','--cached','--check'],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Use explicit ownership for held profile access in maintenance tests'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),source_path_count=len(a['source_paths']),previous_adoption={'path':str(H/'root-adoption-r6.json'),'sha256':sha(H/'root-adoption-r6.json')},correction_capsules=a['correction_capsules']+caps,correction_reviews=a['correction_reviews']+reviews,adopter_r7_sha256=sha(__file__),continue_after_test_failures=True,approved_amendments_r7_sha256=sha(H/'approved-amendments-r7.json'),failed_sixth_gcc_sha256={str(p.relative_to(H)):sha(p) for p in (H/'runs/gcc-r6').iterdir() if p.is_file()})
(H/'root-adoption-r7.json').write_text(json.dumps(a,indent=2)+'\n')
runner=(H/'run-r6.py').read_text().replace("H/'root-adoption-r6.json'","H/'root-adoption-r7.json'").replace("H/'jobs-r6.mk'","H/'jobs-r7.mk'")
runner=runner.replace('import argparse,importlib.util,os,pathlib,re,shutil,stat','import argparse,importlib.util,os,pathlib,re,shutil,stat,fcntl,time')
old=next(line for line in runner.splitlines() if line.startswith("  try:runner.run('tests'"))
call=old.removeprefix('  try:')
block="  try:\n   with (H/'network-subjects.lock').open('a') as fence:\n    fcntl.flock(fence,fcntl.LOCK_EX)\n    schedule={'pid':os.getpid(),'entered_monotonic':time.monotonic(),'shared_lock':str(H/'network-subjects.lock')}\n    try:"+call+"\n    finally:schedule['left_monotonic']=time.monotonic();s.write(out/'subject-scheduling.json',schedule)"
runner=runner.replace(old,block);compile(runner,'run-r7.py','exec');(H/'run-r7.py').write_text(runner)
(H/'jobs-r7.mk').write_text((H/'jobs-r6.mk').read_text().replace('/run-r6.py','/run-r7.py').replace('ATTEMPT ?= r6','ATTEMPT ?= r7'))
print('ADOPTED_REVIEWED_BATCH',a['source_commit'],a['source_path_count'],len(changes))
