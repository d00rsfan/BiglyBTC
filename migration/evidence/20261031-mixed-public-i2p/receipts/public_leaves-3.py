#!/usr/bin/env python3
"""Cached affected Native closure; no router/Java/transfer launch or approval creation."""
import argparse,fcntl,hashlib,json,os,pathlib,re,shutil,signal,stat,subprocess,sys,time
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/mixed-network-20261031'
OLD=R/'build/i2p-multi-20261030';PRIVATE=R/'build/i2p-provider-attachment-20261029/native-followup-20261030/caches'
BASE=[('biglybtc-i2p-classic-host-tests','i2p-classic-host'),('biglybtc-network-policy-tests','network-policy'),('biglybtc-managed-download-tests','managed-download'),('biglybtc-managed-queue-tests','managed-queue'),('biglybtc-managed-tracker-tests','managed-tracker'),('biglybtc-library-commands-bridge-tests','managed-library-command-routing'),('biglybtc-torrent-intake-file-options-tests','torrent-intake-file-options'),('biglybtc-torrent-intake-dialog-tests','torrent-intake-file-options-ui'),('biglybtc-queue-policy-tests','queue-policy'),('biglybtc-queue-slots-tests','queue-slots'),('biglybtc-owned-i2p-source-tests','owned-i2p-profile-source')]
RUNTIME=['persistent-runtime-source-bound','persistent-runtime-manifest-negatives']+['persistent-runtime-reject-'+v for v in ('open','inode','content','fifo','preload','mapped-interposition','same-version-distro')]
MIXED=['provider-mixed-runtime-inputs','provider-mixed-runtime-deployment']+['provider-mixed-reject-foreign-'+v for v in ('owner','torrents','mixed')]
def sha(p):
 h=hashlib.sha256()
 with pathlib.Path(p).open('rb') as f:
  while data:=f.read(1024*1024):h.update(data)
 return h.hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def load(p):return json.loads(pathlib.Path(p).read_bytes())
def git(*args):return subprocess.check_output(['git','-C',str(N),*args],text=True).strip()
def sources():
 paths=git('ls-files','--cached','--others','--exclude-standard','preview/native').splitlines()
 return {p:sha(N/p) for p in sorted(set(paths)) if (N/p).is_file()}
def cache_values(p):return {m[1]:m[2] for x in p.read_text().splitlines() if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',x))}
def image(p):return {'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size}
def original():return {'head':subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip(),'index_sha256':sha(R/'.git/index')}
def associate(cid,source):
 assert re.fullmatch('[0-9a-f]{40}',cid) and git('rev-parse','HEAD')==cid,'Actual current committed source required'
 assert set(git('ls-tree','-r','--name-only',cid,'--','preview/native').splitlines())==set(source),'Committed source inventory differs'
 subprocess.run(['git','-C',str(N),'diff','--quiet',cid,'--','preview/native'],check=True)
 assert sources()==source,'Current source differs from the actual build map'
 return git('rev-parse',cid+':preview/native')
def stamp(path,cid):
 v=load(path);assert v['status']=='PASS_UNSTAMPED' and all(x['exit_code']==0 for x in v['commands'])
 source=load(path.parent/'source-before.json');assert source==load(path.parent/'source-after.json')
 tree=associate(cid,source)
 for row in v['images'].values():assert image(pathlib.Path(row['path']))==row
 for row in v['commands']:assert sha(row['log'])==row['log_sha256']
 for name,h in v['inputs_sha256'].items():assert sha(name)==h
 for name,h in v['dependencies_sha256'].items():assert sha(name)==h
 v.update(status='PASS',source_commit=cid,source_tree=tree,unstamped_receipt=str(path),unstamped_receipt_sha256=sha(path))
 out=path.with_name('result-stamped.json');assert not out.exists();dump(out,v);print(json.dumps({'status':'PASS','receipt':str(out),'sha256':sha(out)}))
def main():
 p=argparse.ArgumentParser();p.add_argument('lane',nargs='?',choices=('private','san','default'));p.add_argument('--attempt',default='r1');p.add_argument('--source-commit');p.add_argument('--stamp',type=pathlib.Path);a=p.parse_args()
 if a.stamp:assert not a.lane and a.source_commit;stamp(a.stamp.resolve(),a.source_commit);return
 assert a.lane and re.fullmatch('r[1-9][0-9]*',a.attempt)
 flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode),'GNU Make4.4 FIFO aggregate3 required'
 assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT'),'No loader injection'
 assert shutil.disk_usage(R).free>=5*1024**3,'Keep5GiB free'
 c=PRIVATE/('san' if a.lane=='san' else 'gcc') if a.lane!='default' else N/'build/persistent-peer-accounting-20261021/bound-gcc'
 assert (c/'CMakeCache.txt').is_file(),'Only the existing task cache is supported'
 out=T/'batch'/a.lane/a.attempt;out.mkdir(parents=True,exist_ok=False)
 source=sources();dump(out/'source-before.json',source);old=original();cv=cache_values(c/'CMakeCache.txt');dump(out/'cache-before.json',cv);inputs={str(pathlib.Path(__file__).resolve()):sha(__file__)}
 targets=['BiglyBTC']+[x[0] for x in BASE]+['biglybtc-runtime-binding-driver','biglybtc-runtime-negative-driver','biglybtc-runtime-interposer','biglybtc-runtime-interposition-driver']
 suites=[x[1] for x in BASE]+RUNTIME;extra=['-DBIGLYBTC_PERSISTENT_COEXISTENCE_RUNTIME=OFF'];record=None
 if a.lane!='default':
  selection_path=T/'final-sdk-inputs.json';selection=load(selection_path);inputs[str(selection_path)]=sha(selection_path)
  approval=pathlib.Path(selection['approval']);manifest=pathlib.Path(selection['runtime_manifest']);approved=load(approval);runtime=load(manifest)
  assert approved['status']=='ROOT_APPROVED_MIXED_I2P_ROLES' and approved['schema']=='BIGLYBTC_MIXED_I2P_ROLE_APPROVAL_V1'
  assert approved['runtime_manifest_sha256']==sha(manifest) and pathlib.Path(approved['runtime_manifest']).resolve()==manifest.resolve()
  record_path=pathlib.Path(runtime['source_build_record']);assert sha(record_path)==approved['source_build_record_sha256'];record=load(record_path)
  assert pathlib.Path(record['install_root']).resolve()==(OLD/'provider/candidate-r1/install').resolve()
  for q in (approval,manifest,record_path,pathlib.Path(record['artifact'])):inputs[str(q)]=sha(q)
  extra+=['-DLibtorrentRasterbar_DIR='+str(OLD/'provider/candidate-r1/install/lib/cmake/LibtorrentRasterbar'),'-DBIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER=ON','-DBIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME=ON','-DBIGLYBTC_PERSISTENT_RUNTIME_MANIFEST='+str(manifest),'-DBIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL='+str(approval),'-DBIGLYBTC_PERSISTENT_MIXED_I2P_APPROVAL_SHA256='+sha(approval),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL='+str(approval),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256='+sha(approval),'-DBIGLYBTC_PERSISTENT_NEGATIVE_LT_LIBRARY='+str(N/'build/dependencies/sysroot/usr/lib/x86_64-linux-gnu/libtorrent-rasterbar.so.2.0.12')]
  targets+=['biglybtc-mixed-network-host-transfer-probe','biglybtc-provider-mixed-runtime-deployment-driver','biglybtc-mixed-interposition-driver']+['biglybtc-mixed-'+v+'-foreign-marker' for v in ('owner','torrents','mixed')];suites+=MIXED
 else:
  extra+=['-DBIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER=OFF','-DBIGLYBTC_PERSISTENT_MIXED_I2P_RUNTIME=OFF']
  default_manifest=pathlib.Path(cv['BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST']);inputs[str(default_manifest)]=sha(default_manifest)
 assert len(suites)==(20 if a.lane=='default' else 25)
 result={'status':'FAIL','lane':a.lane,'cache':str(c),'commands':[],'images':{},'source_commit':None,'source_tree':None,'source_map_sha256':sha(out/'source-before.json'),'inputs_sha256':inputs,'expected_tests':suites,'makeflags':flags,'original_head_index':old};started=time.monotonic()
 def run(label,argv,timeout=1000):
  assert shutil.disk_usage(R).free>=5*1024**3,'Keep5GiB free'
  log=out/(label+'.log');env=dict(os.environ,QT_QPA_PLATFORM='offscreen',NINJA_STATUS='[%f/%t] ',PYTHONDONTWRITEBYTECODE='1');env.pop('CMAKE_BUILD_PARALLEL_LEVEL',None)
  if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
  timed_out=False;start=time.monotonic()
  with log.open('xb') as f:
   child=subprocess.Popen(argv,cwd=N,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
   try:child.wait(timeout=timeout)
   except BaseException:
    timed_out=True
    try:os.killpg(child.pid,signal.SIGTERM)
    except ProcessLookupError:pass
    try:child.wait(timeout=10)
    except subprocess.TimeoutExpired:
     try:os.killpg(child.pid,signal.SIGKILL)
     except ProcessLookupError:pass
     child.wait()
    raise
   finally:result['commands'].append({'argv':argv,'exit_code':child.returncode,'timed_out':timed_out,'elapsed_seconds':round(time.monotonic()-start,2),'log':str(log),'log_sha256':sha(log)})
  print(json.dumps({'lane':a.lane,'stage':label,'exit_code':child.returncode,'log':str(log)}),flush=True)
  if child.returncode:
   lines=log.read_text(errors='replace').splitlines();diagnostics=[x for x in lines if any(w in x for w in ('error:','FAILED:','CMake Error','FAIL!','Failed','undefined reference'))]
   print(json.dumps({'diagnostics':(diagnostics or lines[-10:])[:12]}),flush=True);raise RuntimeError(label+' failed')
 try:
  run('configure',['cmake','-S',str(N/'preview/native'),'-B',str(c),*extra],180)
  configured=cache_values(c/'CMakeCache.txt');assert all(configured.get(k)==cv.get(k) for k in ('CMAKE_CXX_COMPILER','CMAKE_CXX_FLAGS','CMAKE_BUILD_TYPE','CMAKE_EXE_LINKER_FLAGS')),'Compiler/instrumentation settings changed'
  if a.lane=='san':assert 'address' in configured['CMAKE_CXX_FLAGS'] and 'undefined' in configured['CMAKE_CXX_FLAGS']
  if a.lane=='default':assert configured['LibtorrentRasterbar_DIR']==cv['LibtorrentRasterbar_DIR'],'Default shipping provider selection changed'
  run('build',['nice','-n','5','cmake','--build',str(c),'--target',*targets,'--','-k','0'])
  for target in targets:
   name='lib'+target+'.so' if target=='biglybtc-runtime-interposer' or target.endswith('-foreign-marker') else target
   result['images'][target]=image(c/('app' if target=='BiglyBTC' else 'tests')/name)
  dump(out/'images-before.json',result['images']);dump(out/'cache-configured.json',configured);pattern='^('+'|'.join(map(re.escape,suites))+')$'
  with (T/'batch-subjects.lock').open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX);run('ctest',['ctest','--test-dir',str(c),'-R',pattern,'--output-on-failure','--no-tests=error','--output-junit',str(out/'tests.xml'),'-j','1'])
  rows=list(ET.parse(out/'tests.xml').getroot().iter('testcase'));assert len(rows)==len(suites) and {x.get('name') for x in rows}==set(suites),'Exact actual CTest inventory required'
  assert all(not any(x.find(k) is not None for k in ('failure','error','skipped')) for x in rows),'Every selected CTest must actually pass'
  assert all(image(pathlib.Path(v['path']))==v for v in result['images'].values()),'Current test/product images changed during subjects'
  deps={};env=dict(os.environ)
  for row in result['images'].values():
   text=subprocess.check_output(['ldd',row['path']],text=True,env=env);assert 'not found' not in text
   for name in re.findall(r'(?:=>\s+|^\s*)(/\S+)\s+\(',text,re.M):deps[name]=sha(name)
  result['dependencies_sha256']=deps
  for name,h in inputs.items():assert sha(name)==h,'Approved input changed'
  result['status']='PASS_UNSTAMPED'
  if a.source_commit:result.update(status='PASS',source_commit=a.source_commit,source_tree=associate(a.source_commit,source))
 except BaseException as e:result['status']='FAIL';result['failure']=repr(e)
 finally:
  after=sources();dump(out/'source-after.json',after);dump(out/'images-after.json',{name:image(pathlib.Path(row['path'])) for name,row in result['images'].items() if pathlib.Path(row['path']).is_file()})
  if after!=source or original()!=old:result.update(status='FAIL',failure='Native source or original Java HEAD/index changed during batch')
  result.update(elapsed_seconds=round(time.monotonic()-started,2),source_after_sha256=sha(out/'source-after.json'),tests_xml_sha256=sha(out/'tests.xml') if (out/'tests.xml').exists() else None)
  dump(out/'result.json',result);print(json.dumps({'status':result['status'],'receipt':str(out/'result.json'),'elapsed_seconds':result['elapsed_seconds']}),flush=True)
 if result['status'] not in ('PASS','PASS_UNSTAMPED'):raise SystemExit(1)
if __name__=='__main__':main()
