#!/usr/bin/env python3
import shutil as _space
assert _space.disk_usage('/home/yurii/Java/BiglyBT').free>=5*1024**3,'Less than5GiB free; clean generated build files before proceeding'
import subprocess,pathlib,json,hashlib,sys,time,os,signal
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/i2p-multi-20261030';lane=sys.argv[1]
cache=R/('build/i2p-provider-attachment-20261029/native-followup-20261030/caches/'+('san' if lane=='san' else 'gcc')) if lane in ('private','san') else N/'build/persistent-peer-accounting-20261021/bound-gcc'
out=T/lane/('r'+str(sum(1 for _ in (T/lane).glob('r*'))+1));out.mkdir(parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','preview/native'],cwd=N,text=True).splitlines();source={p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()};(out/'source-before.json').write_text(json.dumps(source,indent=2)+'\n')
result={'status':'FAIL','lane':lane,'cache':str(cache),'commands':[],'source_map_sha256':sha(out/'source-before.json')};started=time.monotonic()
def run(label,argv,timeout):
 log=out/(label+'.log');env=dict(os.environ,QT_QPA_PLATFORM='offscreen',NINJA_STATUS='[%f/%t] ')
 if lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 timed_out=False
 with log.open('wb') as f:
  p=subprocess.Popen(argv,cwd=N,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  try:p.wait(timeout=timeout)
  except subprocess.TimeoutExpired:
   timed_out=True
   try:os.killpg(p.pid,signal.SIGTERM)
   except ProcessLookupError:pass
   try:p.wait(timeout=10)
   except subprocess.TimeoutExpired:pass
   try:os.killpg(p.pid,signal.SIGKILL)
   except ProcessLookupError:pass
   p.wait()
 result['commands'].append({'argv':argv,'exit_code':p.returncode,'timed_out':timed_out,'log':str(log),'log_sha256':sha(log)})
 if p.returncode:
  text=log.read_text(errors='replace').splitlines();errors=[s for s in text if any(w in s for w in ('error:','FAILED:','CMake Error','FAIL!','Failed','undefined reference'))]
  print(json.dumps({'lane':lane,'stage':label,'status':'FAIL','log':str(log),'diagnostics':(errors or text[-15:])[:18]},ensure_ascii=False),flush=True);raise RuntimeError(label+' failed')
 print(json.dumps({'lane':lane,'stage':label,'status':'PASS','log':str(log)}),flush=True)
try:
 extra=[]
 if lane in ('private','san'):
  extra=['-DLibtorrentRasterbar_DIR='+str(T/'provider/candidate-r1/install/lib/cmake/LibtorrentRasterbar'),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL='+str(T/'provider/root-private-owner-approval.json'),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256=f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83']
 run('configure',['cmake','-S',str(N/'preview/native'),'-B',str(cache),*extra],180)
 targets=['BiglyBTC','biglybtc-i2p-classic-host-tests']
 if lane in ('private','san'):targets+=['biglybtc-owned-i2p-source-tests','biglybtc-i2p-host-transfer-probe','biglybtc-i2p-multi-host-transfer-probe']
 else:targets+=['biglybtc-library-commands-bridge-tests']
 if lane=='san':targets=['BiglyBTC','biglybtc-i2p-classic-host-tests','biglybtc-owned-i2p-source-tests','biglybtc-i2p-host-transfer-probe','biglybtc-i2p-multi-host-transfer-probe']
 targets+=['biglybtc-torrent-intake-file-options-tests','biglybtc-queue-policy-tests','biglybtc-queue-slots-tests']
 if lane!='san':targets+=['biglybtc-torrent-intake-dialog-tests']
 targets+=['biglybtc-i2p-streaming-manager-tests','biglybtc-i2cp-stream-manager-tests','biglybtc-owned-sam-protocol-tests']
 run('build',['nice','-n','5','cmake','--build',str(cache),'--parallel','3','--target',*targets],1000)
 pattern='^(i2p-classic-host|queue-policy|queue-slots'+('|owned-i2p-profile-source' if lane=='private' else '')+('|managed-library-command-routing' if lane=='default' else '')+')$'
 if lane=='san':pattern='^(i2p-classic-host|owned-i2p-profile-source|queue-policy|queue-slots)$'
 pattern=pattern.replace(')$','|torrent-intake-file-options'+('|torrent-intake-file-options-ui' if lane!='san' else '')+')$')
 pattern=pattern.replace(')$','|pure-i2p-streaming-manager|i2cp-bounded-stream-manager|pure-owned-sam-protocol)$')
 run('ctest',['ctest','--test-dir',str(cache),'-R',pattern,'--output-on-failure','--no-tests=error','-j','1'],1000)
 current={p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()};assert current==source,'Product source changed during build/test batch'
 result['images']={}
 for target in targets:
  image=cache/('app' if target=='BiglyBTC' else 'tests')/target;result['images'][target]={'path':str(image),'sha256':sha(image),'bytes':image.stat().st_size}
 result['status']='PASS'
except Exception as e:result['failure']=str(e)
finally:
 result['elapsed_seconds']=round(time.monotonic()-started,2);(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'status':result['status'],'receipt':str(out/'result.json'),'elapsed_seconds':result['elapsed_seconds']}),flush=True)
sys.exit(0 if result['status']=='PASS' else 1)
