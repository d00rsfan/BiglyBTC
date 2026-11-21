from pathlib import Path
import json,subprocess,time
D=Path(__file__).resolve().parent;plan=json.loads((D/'current-build-plan.json').read_text());B=plan['cache']
for phase,targets in [('APP',['BiglyBTC']),('TESTS',plan['tests'])]:
 memory=int(next(x.split()[1]for x in Path('/proc/meminfo').read_text().splitlines()if x.startswith('MemAvailable:')))
 assert memory>=5*1024*1024,'Memory below5GiB before build phase'
 jobs=1 if phase=='APP'else 3
 command=['cmake','--build',B,'--parallel',str(jobs),'--target',*targets];print('CURRENT_BUILD_PHASE',phase,'jobs',jobs,flush=True);start=time.monotonic();subprocess.run(command,check=True);print('CURRENT_BUILD_PHASE',phase,'PASS',round(time.monotonic()-start,2),'s',flush=True)
