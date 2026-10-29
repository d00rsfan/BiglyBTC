import hashlib,json,pathlib,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
C=R/'build/azureus-profile-io-executor-20261029/amendments/qt-keyword-timer-api/manifest.json'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p):return json.loads(pathlib.Path(p).read_text())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
assert not (H/'root-adoption-r2.json').exists()
a=load(H/'root-adoption.json');assert git('rev-parse','HEAD')==a['source_commit']=='f02385070badd3318a250b2d8dd4146727964ac4' and not git('status','--porcelain')
assert sha(C)=='dbb58ced2e25fcd9b4ad29dcf1a5a61d4df90bf5cc5eb396dacdde2b67ab783e';c=load(C)
for name,pin in c['artifacts'].items():assert sha(C.parent/name)==pin
for name,row in a['source_paths'].items():assert sha(N/name)==row['after_sha256']
history={}
for lane in ('gcc','clang','san'):
 d=H/'runs'/(lane+'-r1');result=load(d/'result.json');assert result['status']=='FAIL' and result['failure']=="AssertionError('build')"
 history[lane]={p.name:sha(p) for p in d.iterdir() if p.is_file()}
for name,row in c['source_paths'].items():
 assert sha(N/name)==row['before_sha256'] and sha(row['after_path'])==row['after_sha256']
for name,row in c['source_paths'].items():
 (N/name).write_bytes(pathlib.Path(row['after_path']).read_bytes());a['source_paths'][name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
subprocess.run(['git','-C',str(N),'diff','--check'],check=True)
subprocess.run(['git','-C',str(N),'add','--',*c['source_paths']],check=True)
subprocess.run(['git','-C',str(N),'-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Fix Qt keyword collision and chrono timer API use'],check=True)
a.update(source_commit=git('rev-parse','HEAD'),initial_adoption={'path':str(H/'root-adoption.json'),'sha256':sha(H/'root-adoption.json')},correction_capsules=[{'path':str(C),'sha256':sha(C)}],adopter_r2_sha256=sha(__file__),failed_r1_history_sha256=history)
(H/'root-adoption-r2.json').write_text(json.dumps(a,indent=2)+'\n')
for stem in ('run','private','offline'):
 text=(H/(stem+'.py')).read_text().replace("H/'root-adoption.json'","H/'root-adoption-r2.json'").replace("H/'run.py'","H/'run-r2.py'")
 (H/(stem+'-r2.py')).write_text(text)
print('ADOPTED',a['source_commit'],'paths',len(a['source_paths']))
