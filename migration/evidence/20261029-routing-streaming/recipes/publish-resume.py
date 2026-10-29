"""Resume only publication after byte-exact loader-log whitespace refusal."""
import hashlib,json,pathlib,re,shutil,subprocess
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;E=pathlib.Path('migration/evidence/20261029-routing-streaming')
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def git(*a,**kw):return subprocess.run(['git','-C',str(N),*a],check=True,**kw)
assert not (H/'publish.json').exists()
source='514a238f0efa749e7a7fb1e68f8123af78e766da';assert subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()==source
v=json.loads((R/E/'verification.json').read_text());assert v['source_commit']==source and v['promotion']['manifest_sha256']==sha(N/'dist/BUILD_MANIFEST.json')
initial=(H/'diff-check.log').read_text();issues=[x for x in initial.splitlines() if re.match(r'^migration/.*:\d+:',x)]
assert issues and all(re.match(r'^migration/evidence/20261029-routing-streaming/r3/actor-loader\.\d+:\d+: trailing whitespace\.$',x) for x in issues)
for name,p in [('recipes/publish-resume.py',pathlib.Path(__file__)),('publication/publish-initial.log',H/'publish.log'),('publication/diff-check-initial.log',H/'diff-check.log')]:
 target=R/E/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target);v['artifacts_sha256'][name]=sha(target)
v['publication_preflight']={'initial_exit':2,'reason':'Byte-exact glibc actor-loader.PID diagnostic traces contain trailing tabs. Their bytes remain unchanged; whitespace checks exclude these raw logs like other retained logs.','runtime_replayed':False}
(R/E/'verification.json').write_text(json.dumps(v,indent=2)+'\n')
for p in (R/E).rglob('*'):
 if p.is_file():q=N/E/p.relative_to(R/E);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
git('add','--',str(E))
with (H/'diff-check-resume.log').open('wb') as f:git('diff','--cached','--check','--','.',':(exclude)**/*.log',':(exclude)**/*.xml',':(exclude)**/*.patch',':(exclude)**/commands.txt',':(exclude)**/original-source/**',':(exclude)**/actor-loader.*',stdout=f,stderr=subprocess.STDOUT)
git('-c','user.name=Codex','-c','user.email=codex@localhost','-c','commit.gpgsign=false','commit','-q','-m','Qualify owned routing and original I2P Streaming interoperability')
commit=subprocess.check_output(['git','-C',str(N),'rev-parse','HEAD'],text=True).strip()
with (H/'push.log').open('wb') as f:git('push','origin','main',stdout=f,stderr=subprocess.STDOUT)
assert not subprocess.check_output(['git','-C',str(N),'status','--porcelain'],text=True).strip()
(H/'publish.json').write_text(json.dumps({'commit':commit,'branch':'main','pushed':True,'source_commit':source,'publication_recipe':str(pathlib.Path(__file__))},indent=2)+'\n');print('COMMITTED_PUSHED_CLEAN',commit)
