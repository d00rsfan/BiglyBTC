from pathlib import Path
import subprocess,json,hashlib,os,time
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/host-i2p-20261030/ci-tab-expectations';C=N/'build/persistent-peer-accounting-20261021/bound-gcc'
names=['history-view-ui','history-week-view-ui','history-month-view-ui','engine-activity-view','manual-tag-window','country-statistics-window','azureus-statistics-channel','live-speed-manager-channel','startup-statistics'];targets=['biglybtc-'+name+'-tests' for name in names]
pattern='^(saved-history-page|saved-history-week-page|saved-history-month-page|engine-activity-view|manual-tag-window|country-statistics-window|azureus-statistics-channel|live-speed-manager-channel|startup-statistics-worker)$'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','preview/native'],cwd=N,text=True).splitlines();sources={p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()};(T/'source-before.json').write_text(json.dumps(sources,indent=2)+'\n');x={'status':'FAIL','cache':str(C),'commands':[]};start=time.monotonic()
try:
 for label,argv in [('build',['nice','-n','5','cmake','--build',str(C),'--parallel','3','--target',*targets]),('ctest',['ctest','--test-dir',str(C),'-R',pattern,'--output-on-failure','--no-tests=error','-j','1'])]:
  log=T/(label+'.log');env=dict(os.environ,QT_QPA_PLATFORM='offscreen',NINJA_STATUS='[%f/%t] ')
  with log.open('wb') as f:p=subprocess.run(argv,cwd=N,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=1000)
  x['commands'].append({'argv':argv,'exit_code':p.returncode,'log':str(log),'log_sha256':sha(log)});print(json.dumps({'stage':label,'exit_code':p.returncode,'log':str(log)}),flush=True)
  if p.returncode:
   errors=[s for s in log.read_text(errors='replace').splitlines() if any(k in s for k in ['error:','FAIL!','Failed','FAILED:'])];print('\n'.join(errors[:15]),flush=True);raise RuntimeError(label)
 assert {p:sha(N/p) for p in sources}==sources
 x['images']={target:sha(C/'tests'/target) for target in targets};x['status']='PASS'
except Exception as e:x['failure']=str(e)
finally:x['elapsed_seconds']=round(time.monotonic()-start,2);(T/'result.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'elapsed_seconds':x['elapsed_seconds']}),flush=True)
raise SystemExit(0 if x['status']=='PASS' else 1)
