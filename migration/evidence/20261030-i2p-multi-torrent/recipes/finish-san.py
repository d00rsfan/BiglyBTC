from pathlib import Path
import json,time,subprocess,sys
T=Path('/home/yurii/Java/BiglyBT/build/i2p-multi-20261030');g=T/'acceptance/runs/gcc-r15';start=time.monotonic()
print('SAN queued until genuine GCC shared controls pass.',flush=True)
while not (g/'shared-controls-ready.json').exists():
 if (g/'result.json').exists():raise SystemExit('GCC ended before shared controls; SAN not started')
 if time.monotonic()-start>420:raise SystemExit('GCC control observation budget exceeded; SAN not started')
 time.sleep(5)
print('GCC genuine shared controls passed; continuing cached SAN qualification.',flush=True)
while not (T/'default/r4/result.json').exists():time.sleep(5)
assert json.loads((T/'default/r4/result.json').read_text())['status']=='PASS'
assert not (T/'san/r4').exists()
subprocess.run([sys.executable,str(T/'run-batch.py'),'san'],check=True)
subprocess.run([sys.executable,str(T/'launch-acceptance.py'),'san-r1','r4','san'],check=True)
