from pathlib import Path
import json,os,subprocess,time
D=Path(__file__).resolve().parent;R=D.parents[1];W=R/'build/public-nat-20261114'
for task in json.loads((D/'variant-builds.json').read_text()):
 log=W/(task['name']+'.log');start=time.monotonic()
 with log.open('w') as stream:result=subprocess.run(task['argv'],cwd=task['cwd'],stdout=stream,stderr=subprocess.STDOUT)
 print(task['name'],'PASS' if result.returncode==0 else 'FAIL',result.returncode,round(time.monotonic()-start,2),'s',flush=True)
 if result.returncode:
  data=log.read_text(errors='replace').splitlines();errors=[s[:450] for s in data if 'error:' in s or 'undefined reference' in s];print('\n'.join(errors[:4] or data[-5:]));raise SystemExit(result.returncode)
