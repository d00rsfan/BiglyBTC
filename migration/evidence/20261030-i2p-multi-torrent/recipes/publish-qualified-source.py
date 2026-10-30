from pathlib import Path
import json,time,subprocess,sys
T=Path('/home/yurii/Java/BiglyBT/build/i2p-multi-20261030');N=Path('/home/yurii/Java/BiglyBTC');result=T/'acceptance/runs/gcc-r16/result.json';start=time.monotonic()
print('Publication queued until genuine GCC transfer/control/restart PASS.',flush=True)
while not result.exists():
 if time.monotonic()-start>1100:raise SystemExit('GCC observation limit; publication not performed')
 time.sleep(10)
v=json.loads(result.read_text());assert v['status']=='PASS_REAL_ISOLATED_ROUTER_MULTI_I2P_HOST_TRANSFER',v.get('failure')
assert not subprocess.check_output(['git','status','--porcelain'],cwd=N,text=True).strip()
code=subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip();print('Genuine GCC qualification PASS; publishing main '+code,flush=True)
subprocess.run(['git','push','origin','main'],cwd=N,check=True)
subprocess.run([sys.executable,str(T/'watch-ci.py'),code],check=True)
