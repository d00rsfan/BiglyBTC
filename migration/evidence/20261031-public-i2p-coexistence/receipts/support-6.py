#!/usr/bin/env python3
import shutil as _space
assert _space.disk_usage('/home/yurii/Java/BiglyBT').free>=5*1024**3,'Less than5GiB free; clean generated build files before proceeding'
import subprocess,pathlib,json,hashlib,sys,time,os,signal
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/coexist-20261031';Old=R/'build/i2p-multi-20261030';lane=sys.argv[1]
cache=R/('build/i2p-provider-attachment-20261029/native-followup-20261030/caches/'+('san' if lane=='san' else 'gcc')) if lane in ('private','san') else N/'build/persistent-peer-accounting-20261021/bound-gcc'
out=T/lane/('r'+str(sum(1 for _ in (T/lane).glob('r*'))+1));out.mkdir(parents=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','preview/native'],cwd=N,text=True).splitlines();source={p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()};(out/'source-before.json').write_text(json.dumps(source,indent=2)+'\n')
jobs=int(os.environ.get('BIGLYBTC_BUILD_JOBS',('2' if lane=='san' else '3')));assert 1<=jobs<=3
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
 extra=['-DBIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME=OFF']
 if lane in ('private','san'):
  extra=['-DLibtorrentRasterbar_DIR='+str(Old/'provider/candidate-r1/install/lib/cmake/LibtorrentRasterbar'),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL='+str(Old/'provider/root-private-owner-approval.json'),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256=f3ca7de94ca6beb12e0375293c9c74c3f78a34f7e4952e1a122ff0ae9dff1f83']
  approval=T/'sdk-role-integration/root-coexistence-approval.json'
  extra+=['-DBIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY='+str(N/'build/dependencies/sysroot/usr/lib/x86_64-linux-gnu/libtorrent-rasterbar.so.2.0.12'),'-DBIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME=ON','-DBIGLYBTC_PERSISTENT_RUNTIME_MANIFEST='+str(T/'sdk-role-integration/runtime-manifest.json'),'-DBIGLYBTC_PERSISTENT_COEXISTENCE_APPROVAL='+str(approval),'-DBIGLYBTC_PERSISTENT_COEXISTENCE_APPROVAL_SHA256='+sha(approval)]
 run('configure',['cmake','-S',str(N/'preview/native'),'-B',str(cache),*extra],180)
 targets=['BiglyBTC','biglybtc-i2p-classic-host-tests','biglybtc-managed-queue-tests','biglybtc-managed-download-tests','biglybtc-managed-tracker-tests','biglybtc-library-commands-bridge-tests','biglybtc-torrent-intake-file-options-tests','biglybtc-queue-policy-tests','biglybtc-queue-slots-tests']
 targets+=['biglybtc-i2cp-initial-channel-tests','biglybtc-i2cp-datagram-channel-tests','biglybtc-i2cp-receive-channel-tests','biglybtc-i2cp-tracker-channel-tests','biglybtc-i2cp-stream-manager-tests']
 targets+=['biglybtc-runtime-binding-driver','biglybtc-runtime-negative-driver','biglybtc-runtime-interposer','biglybtc-runtime-interposition-driver']
 suites=['i2cp-initial-channel','i2cp-lease-backed-datagram-channel','i2cp-bounded-datagram-receiver','i2cp-explicit-tracker-channel','i2cp-bounded-stream-manager','persistent-runtime-source-bound','persistent-runtime-reject-(open|inode|content|fifo|preload|mapped-interposition|same-version-distro)','i2p-classic-host','managed-queue','managed-download','managed-tracker','managed-library-command-routing','torrent-intake-file-options','queue-policy','queue-slots']
 if lane in ('private','san'):
  targets+=['biglybtc-provider-coexistence-runtime-deployment-driver','biglybtc-coexistence-interposition-driver','biglybtc-owned-i2p-source-tests','biglybtc-coexist-host-transfer-probe']
  suites+=['owned-i2p-profile-source','provider-coexistence-(runtime-inputs|runtime-deployment|reject-foreign-owner|reject-foreign-torrents)']
 if lane!='san':
  targets+=['biglybtc-torrent-intake-dialog-tests'];suites+=['torrent-intake-file-options-ui']
 run('build',['nice','-n','5','cmake','--build',str(cache),'--parallel',str(jobs),'--target',*targets,'--','-k','0'],1000)
 pattern='^('+'|'.join(suites)+')$'
 run('ctest',['ctest','--test-dir',str(cache),'-R',pattern,'--output-on-failure','--no-tests=error','-j','1'],1000)
 current={p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()};assert current==source,'Product source changed during build/test batch'
 result['images']={}
 for target in targets:
  image=cache/('app' if target=='BiglyBTC' else 'tests')/('libbiglybtc-runtime-interposer.so' if target=='biglybtc-runtime-interposer' else target);result['images'][target]={'path':str(image),'sha256':sha(image),'bytes':image.stat().st_size}
 result['status']='PASS'
except Exception as e:result['failure']=str(e)
finally:
 result['elapsed_seconds']=round(time.monotonic()-started,2);(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'status':result['status'],'receipt':str(out/'result.json'),'elapsed_seconds':result['elapsed_seconds']}),flush=True)
sys.exit(0 if result['status']=='PASS' else 1)
