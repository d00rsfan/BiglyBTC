from pathlib import Path
import json,subprocess,sys,time
D=Path(__file__).resolve().parent;plan=json.loads((D/'qualification-build-plan.json').read_text())[sys.argv[1]];B=plan['cache']
for phase,targets in [('APP',['BiglyBTC']),('TESTS',plan['tests'])]:
 memory=int(next(x.split()[1]for x in Path('/proc/meminfo').read_text().splitlines()if x.startswith('MemAvailable:')));assert memory>=5*1024*1024,'Memory below5GiB';jobs=1 if phase=='APP' else 3
 command=['cmake','--build',B,'--parallel',str(jobs),'--target',*targets];print('QUALIFICATION_BUILD_PHASE',sys.argv[1],phase,'jobs',jobs,flush=True);start=time.monotonic();subprocess.run(command,check=True);print('QUALIFICATION_BUILD_PHASE',sys.argv[1],phase,'PASS',round(time.monotonic()-start,2),'s',flush=True)
