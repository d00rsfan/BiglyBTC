from pathlib import Path
import json,os,subprocess,sys,time
w=Path(__file__).resolve().parent
spec=json.loads((w/sys.argv[1]).read_text())
names=[task['name'] for task in spec]
assert len(names)==len(set(names)),'Check task names must be unique to preserve logs'
for task in spec:
 p=w/(task['name']+'.log');env=dict(os.environ);env.update(task.get('env',{}));start=time.monotonic()
 with p.open('w') as f:result=subprocess.run(task['argv'],env=env,stdout=f,stderr=subprocess.STDOUT)
 print(task['name'], 'PASS' if result.returncode==0 else 'FAIL', result.returncode, round(time.monotonic()-start,2),'s',flush=True)
 if result.returncode:
  lines=p.read_text(errors='replace').splitlines()
  errors=[(i,s) for i,s in enumerate(lines) if any(t in s for t in ['error:','FAIL!','CMake Error','AssertionError','SyntaxError'])]
  for i,s in errors[:3]:print(s[:420]);print('\n'.join(t[:220] for t in lines[i+1:i+3]))
  if not errors:print('\n'.join(s[:260] for s in lines[-8:]))
  raise SystemExit(result.returncode)
