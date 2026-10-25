from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,time
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');case=sys.argv[1];assert re.fullmatch(r'[a-zA-Z0-9_-]+',case)
work=Path('/tmp')/('biglybtc-managed-handoff-defaults-'+case);dest=w/('defaults-'+case);assert not work.exists() and not dest.exists()
cmd=['python3',str(w/'oracle-next/run.py'),'--reference-root','/home/yurii/Java/BiglyBT','--driver',str(n/'build/persistent-peer-accounting-20261021/bound-gcc/tests/biglybtc-storage-defaults-handoff-driver'),'--work',str(work)]
for name in sys.argv[2:]:cmd+=['--case',name]
env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1';start=time.monotonic()
with(w/('defaults-'+case+'.log')).open('w') as f:rc=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
if work.exists():shutil.copytree(work,dest)
r={'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,2),'runner_sha256':hashlib.sha256((w/'oracle-next/run.py').read_bytes()).hexdigest(),'durable_copy':str(dest) if dest.exists() else None};(w/('defaults-'+case+'-receipt.json')).write_text(json.dumps(r,indent=2)+'\n');print('Actual original defaults',case,'exit',rc,'seconds',r['seconds'])
if rc:print('\n'.join((w/('defaults-'+case+'.log')).read_text(errors='replace').splitlines()[-18:]))
raise SystemExit(rc)
