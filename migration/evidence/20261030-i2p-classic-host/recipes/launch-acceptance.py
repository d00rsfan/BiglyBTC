from pathlib import Path
import json,subprocess,hashlib,sys
T=Path('/home/yurii/Java/BiglyBT/build/host-i2p-20261030');N=Path('/home/yurii/Java/BiglyBTC');attempt=sys.argv[1];batch=sys.argv[2];lane=sys.argv[3] if len(sys.argv)>3 else 'private'
b=json.loads((T/lane/batch/'result.json').read_text());assert b['status']=='PASS'
b['source_commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip();b['source_tree']=subprocess.check_output(['git','rev-parse','HEAD:preview/native'],cwd=N,text=True).strip();assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=all','--','preview/native'],cwd=N,text=True).strip()
sources=json.loads((T/lane/batch/'source-before.json').read_text());assert all(hashlib.sha256((N/p).read_bytes()).hexdigest()==sha for p,sha in sources.items())
receipt=T/lane/batch/'build-receipt.json';assert not receipt.exists();receipt.write_text(json.dumps(b,indent=2)+'\n');image=b['images']['biglybtc-i2p-host-transfer-probe'];out=T/'acceptance/runs'/attempt;log=T/'acceptance'/('launch-'+attempt+'.log')
args=['python3',str(T/'acceptance/run.py'),'--source-commit',b['source_commit'],'--build-receipt',str(receipt),'--probe',image['path'],'--probe-sha256',image['sha256'],'--provider-record','/home/yurii/Java/BiglyBT/build/i2p-provider-attachment-20261029/provider-candidate-r2/source-build-record.json','--provider-approval','/home/yurii/Java/BiglyBT/build/i2p-provider-attachment-20261029/provider-candidate-r2/root-private-owner-approval.json','--provider-approval-sha256','6ff4c4d044777e2d777a7b61271c804736a102be2821e65840529cc0ecf529d5','--java','/usr/lib/jvm/java-26-openjdk-amd64/bin/java','--javac','/usr/lib/jvm/java-26-openjdk-amd64/bin/javac','--acknowledge-warmed-local-cache-only','--output',str(out)]
(T/'acceptance'/('launch-'+attempt+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
import os
env=dict(os.environ)
if lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
with log.open('wb') as f:r=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=600)
print(json.dumps({'exit_code':r.returncode,'result':str(out/'result.json'),'log':str(log)}))
if (out/'result.json').exists():
 x=json.loads((out/'result.json').read_text());print(json.dumps({k:x[k] for k in ('status','failure','failure_class','port') if k in x}))
if r.returncode:print('\n'.join(log.read_text(errors='replace').splitlines()[-9:]))
raise SystemExit(r.returncode)
