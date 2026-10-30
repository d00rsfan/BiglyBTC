import shutil as _space
assert _space.disk_usage('/home/yurii/Java/BiglyBT').free>=5*1024**3,'Less than5GiB free; clean generated build files before proceeding'
from pathlib import Path
import json,subprocess,hashlib,sys
T=Path('/home/yurii/Java/BiglyBT/build/i2p-multi-20261030');N=Path('/home/yurii/Java/BiglyBTC');attempt=sys.argv[1];batch=sys.argv[2];lane=sys.argv[3] if len(sys.argv)>3 else 'private'
b=json.loads((T/lane/batch/'result.json').read_text());assert b['status']=='PASS'
b['source_commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip();b['source_tree']=subprocess.check_output(['git','rev-parse','HEAD:preview/native'],cwd=N,text=True).strip();assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=all','--','preview/native'],cwd=N,text=True).strip()
sources=json.loads((T/lane/batch/'source-before.json').read_text());assert all(hashlib.sha256((N/p).read_bytes()).hexdigest()==sha for p,sha in sources.items())
receipt=T/lane/batch/'build-receipt.json'
if receipt.exists():assert json.loads(receipt.read_text())==b,'Existing receipt differs'
else:receipt.write_text(json.dumps(b,indent=2)+'\n')
if lane=='san':
 import time
 ready=T/'acceptance/fixture-r16-ready.json';start=time.monotonic()
 while not ready.exists():
  if time.monotonic()-start>300:raise SystemExit('Reviewed tracker fixture not ready; SAN subject not started')
  time.sleep(5)
 approval=json.loads(ready.read_text())
 for name,expected in approval['files_sha256'].items():assert hashlib.sha256((T/'acceptance'/name).read_bytes()).hexdigest()==expected
image=b['images']['biglybtc-i2p-multi-host-transfer-probe'];out=T/'acceptance/runs'/attempt;log=T/'acceptance'/('launch-'+attempt+'.log')
args=['python3',str(T/'acceptance/run.py'),'--source-commit',b['source_commit'],'--build-receipt',str(receipt),'--probe',image['path'],'--probe-sha256',image['sha256'],'--provider-record',str(T/'provider/candidate-r1/source-build-record.json'),'--provider-approval',str(T/'provider/root-private-owner-approval.json'),'--provider-approval-sha256','f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83','--provider-source',str(T/'provider/amendments/tracker-result-admission/manifest.json'),'--provider-source-sha256','63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3','--java','/usr/lib/jvm/java-26-openjdk-amd64/bin/java','--javac','/usr/lib/jvm/java-26-openjdk-amd64/bin/javac','--acknowledge-warmed-local-cache-only','--output',str(out)]
(T/'acceptance'/('launch-'+attempt+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
import os
env=dict(os.environ)
if lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
with log.open('wb') as f:r=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1300)
print(json.dumps({'exit_code':r.returncode,'result':str(out/'result.json'),'log':str(log)}))
if (out/'result.json').exists():
 x=json.loads((out/'result.json').read_text());print(json.dumps({k:x[k] for k in ('status','failure','failure_class','port') if k in x}))
if r.returncode:print('\n'.join(log.read_text(errors='replace').splitlines()[-9:]))
raise SystemExit(r.returncode)
