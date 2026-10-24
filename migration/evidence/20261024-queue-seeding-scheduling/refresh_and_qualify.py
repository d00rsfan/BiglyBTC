"""Reuse the Clang build's two slots for small GCC links, then qualify private runtime."""
import hashlib,json,subprocess,time
from pathlib import Path
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');b=n/'build/persistent-peer-accounting-20261021/bound-gcc'
source=(w/'final-source-commit.txt').read_text().strip();frozen=json.loads((w/'final-source.json').read_text())
start=time.monotonic();notice=start
while True:
 p=w/'queue-clang-receipt.json'
 if p.exists():
  try:r=json.loads(p.read_text())
  except json.JSONDecodeError:r={}
  if r.get('commands'):
   assert r['commands'][0]['exit']==0,'Clang build failed; do not refresh/qualify'
   break
 assert time.monotonic()-start<1800,'Clang dependency timed out'
 if time.monotonic()-notice>45:print('Waiting for Clang compile slots; full output stays in its build log',flush=True);notice=time.monotonic()
 time.sleep(3)
assert all(hashlib.sha256((n/p).read_bytes()).hexdigest()==v for p,v in frozen.items())
plan=json.loads((w/'portable/plan.json').read_text());targets=list(dict.fromkeys(plan['products']+list(plan['test_targets'].values())+['biglybtc-queue-settings-roundtrip-driver']))
cmd=['nice','-n','5','cmake','--build',str(b),'--parallel','2','--target',*targets]
t=time.monotonic()
with (w/'portable-gcc-refresh.log').open('x') as out:rc=subprocess.run(cmd,cwd=n,stdout=out,stderr=subprocess.STDOUT).returncode
(w/'portable-gcc-refresh.json').write_text(json.dumps({'command':cmd,'exit':rc,'seconds':time.monotonic()-t,'source':source,'source_map_sha256':hashlib.sha256((w/'final-source.json').read_bytes()).hexdigest(),'compiler_jobs':2,'alongside_sanitizer_jobs':1},indent=2)+'\n')
print('Portable GCC refresh exit',rc,flush=True);assert rc==0
raise SystemExit(subprocess.run(['python3',str(w/'portable/qualify.py'),'--source',source,'--source-map',str(w/'final-source.json'),'--build',str(b),'--attempt','queue-final'],cwd=n).returncode)
