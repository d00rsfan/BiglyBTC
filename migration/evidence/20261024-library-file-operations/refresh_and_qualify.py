from pathlib import Path
import hashlib,json,subprocess,time
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');b=n/'build/persistent-peer-accounting-20261021/bound-gcc';source=(w/'final-source-commit.txt').read_text().strip();frozen=json.loads((w/'final-source.json').read_text());assert all(hashlib.sha256((n/p).read_bytes()).hexdigest()==v for p,v in frozen.items())
for case in ['gcc-r4','clang-r2','sanitized-r2']:assert all(c['exit']==0 for c in json.loads((w/(case+'-receipt.json')).read_text())['commands'])
assert json.loads((w/'java-handoff/run-r4/result.json').read_text())['status']=='PASS_ACTUAL_NATIVE_FILE_OPERATIONS_ORIGINAL_JAVA'
plan=json.loads((w/'portable/plan.json').read_text());targets=list(dict.fromkeys(plan['products']+list(plan['test_targets'].values())+['biglybtc-file-operations-driver']))
cmd=['nice','-n','5','cmake','--build',str(b),'--parallel','3','--target',*targets];start=time.monotonic()
with (w/'portable-gcc-refresh.log').open('w') as f:rc=subprocess.run(cmd,cwd=n,stdout=f,stderr=subprocess.STDOUT).returncode
(w/'portable-gcc-refresh.json').write_text(json.dumps({'command':cmd,'exit':rc,'seconds':time.monotonic()-start,'source':source,'source_map_sha256':hashlib.sha256((w/'final-source.json').read_bytes()).hexdigest(),'compiler_jobs':3},indent=2)+'\n');print('Portable GCC refresh exit',rc,flush=True);assert rc==0
raise SystemExit(subprocess.run(['python3',str(w/'portable/qualify.py'),'--source',source,'--source-map',str(w/'final-source.json'),'--build',str(b),'--attempt','fileops-final'],cwd=n).returncode)
