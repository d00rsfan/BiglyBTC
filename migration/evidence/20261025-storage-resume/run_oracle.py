from pathlib import Path
import hashlib,json,shutil,subprocess,sys,time
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
phase,case=sys.argv[1:];assert phase in ['codec','managed','all'] and '/' not in case
b=n/'build/persistent-peer-accounting-20261021/bound-gcc/tests'
work=Path('/tmp')/('biglybtc-managed-handoff-storage-'+case);assert not work.exists()
dest=w/('oracle-'+case);assert not dest.exists()
cmd=['python3',str(w/'oracle/run.py'),'--reference-root','/home/yurii/Java/BiglyBT','--codec-driver',str(b/'biglybtc-original-file-io-driver'),'--managed-driver',str(b/'biglybtc-storage-handoff-driver'),'--phase',phase,'--work',str(work)]
start=time.monotonic()
with(w/('oracle-'+case+'.log')).open('w') as f:rc=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
if work.exists():shutil.copytree(work,dest)
receipt={'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,2),'runner_sha256':hashlib.sha256((w/'oracle/run.py').read_bytes()).hexdigest(),'durable_copy':str(dest) if dest.exists() else None}
(w/('oracle-'+case+'-receipt.json')).write_text(json.dumps(receipt,indent=2)+'\n');print('Actual original',phase,case,'exit',rc,'seconds',receipt['seconds'])
if rc:print('\n'.join((w/('oracle-'+case+'.log')).read_text(errors='replace').splitlines()[-18:]))
raise SystemExit(rc)
