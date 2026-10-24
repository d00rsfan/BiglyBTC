import json,subprocess,time
from pathlib import Path
w=Path(__file__).resolve().parent;start=time.monotonic();notice=start
while True:
 p=w/'sanitized-r1-receipt.json'
 if p.exists():
  r=json.loads(p.read_text())
  if r.get('commands'):
   assert r['commands'][0]['exit']==0,'Sanitizer build failed; preserve its diagnostic'
   break
 assert time.monotonic()-start<2400,'Compile-slot wait timeout'
 if time.monotonic()-notice>45:print('Waiting for a free compile slot for the three Java helpers',flush=True);notice=time.monotonic()
 time.sleep(3)
cmd=['python3',str(w/'java-handoff/run.py'),'--driver','/home/yurii/Java/BiglyBTC/build/persistent-peer-accounting-20261021/bound-gcc/tests/biglybtc-file-operations-driver','--output',str(w/'java-handoff/run-r3')]
start=time.monotonic()
with (w/'java-final.log').open('w') as log:rc=subprocess.run(cmd,cwd=w.parents[1],stdout=log,stderr=subprocess.STDOUT).returncode
(w/'java-final.json').write_text(json.dumps({'command':cmd,'exit':rc,'seconds':time.monotonic()-start},indent=2)+'\n');print('Original Java final handoff exit',rc,flush=True)
if rc:print('\n'.join((w/'java-final.log').read_text().splitlines()[-12:]),flush=True)
raise SystemExit(rc)
