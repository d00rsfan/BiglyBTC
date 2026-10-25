from pathlib import Path
import hashlib,json,os,subprocess
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');p=json.loads((w/'source-commit-plan.json').read_text());git=lambda *a:subprocess.check_output(['git',*a],cwd=n,text=True).strip()
for case in ['gcc-r9','gcc-functional-r9','clang-r9b','clang-r9-tests','asan-r9b','asan-r9-tests','oracle-codec-r9','oracle-managed-r8','defaults-all-r8']:
 r=json.loads((w/(case+'-receipt.json')).read_text());assert r['exit']==0,case
assert git('rev-parse','HEAD')==p['parent'] and git('branch','--show-current')=='main'
assert git('config','--get','remote.origin.url') in ['https://github.com/d00rsfan/BiglyBTC.git','git@github.com:d00rsfan/BiglyBTC.git','ssh://git@github.com/d00rsfan/BiglyBTC.git']
assert not git('diff','--cached','--name-only')
changed={r[3:].decode() for r in subprocess.check_output(['git','status','--porcelain=v1','-z','--untracked-files=all'],cwd=n).split(b'\0') if r};assert changed==set(p['paths_sha256'])
for rel,sha in p['paths_sha256'].items():assert hashlib.sha256((n/rel).read_bytes()).hexdigest()==sha,rel
subprocess.run(['git','fetch','origin','main'],cwd=n,check=True,stdout=subprocess.DEVNULL);assert git('rev-parse','FETCH_HEAD')==p['parent'],'Remote advanced; inspect before integrating'
subprocess.run(['git','add','--',*sorted(changed)],cwd=n,check=True)
env=os.environ.copy();env.update(GIT_AUTHOR_NAME='Codex',GIT_AUTHOR_EMAIL='codex@localhost',GIT_COMMITTER_NAME='Codex',GIT_COMMITTER_EMAIL='codex@localhost');subprocess.run(['git','-c','commit.gpgsign=false','commit','-m',p['message']],cwd=n,env=env,check=True)
source=git('rev-parse','HEAD');assert len(source)==40 and not git('status','--porcelain')
for rel,sha in p['paths_sha256'].items():assert hashlib.sha256((n/rel).read_bytes()).hexdigest()==sha,rel
(w/'final-source-commit.txt').write_text(source+'\n');(w/'source-commit-receipt.json').write_text(json.dumps({'source':source,'parent':p['parent'],'branch':'main','paths':len(changed),'source_plan_sha256':hashlib.sha256((w/'source-commit-plan.json').read_bytes()).hexdigest(),'pushed':False},indent=2)+'\n');print('Committed validated source',source,'on main; publication gates remain')
