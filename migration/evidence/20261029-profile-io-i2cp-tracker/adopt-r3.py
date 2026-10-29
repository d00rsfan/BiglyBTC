import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
a=load(H/'root-adoption-r2.json');assert not (H/'root-adoption-r3.json').exists();assert git('rev-parse','HEAD')==a['source_commit']=='4b2f4b5748d8676d3436b174b94805148453cdd4' and not git('status','--porcelain')
refs=[(R/'build/azureus-profile-io-executor-20261029/amendments/invalid-lease-and-source-rejection/manifest.json','bd969c1554e5d7dcfe5d57189c1f4241b14c6372597235eaef18fc514d0b530d'),(H/'amendments/state-writer-fixture/manifest.json','f4b4f5ed6774fab322b604d9719e8bdeee0a119e2d60f683347af358d44b46ed')]
review=R/'build/azureus-profile-io-executor-20261029-review/source-rejection-review.json';assert sha(review)=='fc1552d68431e749f0e34b9eb95aa2ff156fd5f8cfb2a967d0710b1b775b6a0f'
rows={};history={}
for lane in ('gcc','clang','san'):
 d=H/'runs'/(lane+'-r2');result=load(d/'result.json');assert result['status']=='FAIL' and result['failure']=="AssertionError('tests')";assert [x['exit'] for x in load(d/'commands.json')]==[0,8]
 history[lane]={p.name:sha(p) for p in d.iterdir() if p.is_file()}
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
for cap,pin in refs:
 assert sha(cap)==pin;m=load(cap)
 for name,digest in m['artifacts'].items():assert sha(cap.parent/name)==digest
 for name,row in m['source_paths'].items():
  assert name not in rows and sha(N/name)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256'];rows[name]=row
for name,row in rows.items():
 (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'diff','--check'],check=True)
subprocess.run(['git','-C',str(N),'add','--',*rows],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Preserve profile rejection semantics and fix writer guard fixture'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),previous_adoption={'path':str(H/'root-adoption-r2.json'),'sha256':sha(H/'root-adoption-r2.json')},adopter_r3_sha256=sha(__file__),failed_r2_history_sha256=history)
a['source_reviews'].append({'path':str(review),'sha256':sha(review)});a['correction_capsules'].extend({'path':str(cap),'sha256':pin} for cap,pin in refs)
(H/'root-adoption-r3.json').write_text(json.dumps(a,indent=2)+'\n')
for stem in ('run','private','offline'):
 text=(H/(stem+'-r2.py')).read_text().replace("H/'root-adoption-r2.json'","H/'root-adoption-r3.json'").replace("H/'run-r2.py'","H/'run-r3.py'");(H/(stem+'-r3.py')).write_text(text)
print('ADOPTED',a['source_commit'],'correction leaves',len(rows))
