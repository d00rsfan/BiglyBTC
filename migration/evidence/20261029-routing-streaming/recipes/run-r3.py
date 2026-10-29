#!/usr/bin/env python3
"""Root-run affected routing/Streaming qualification; no actions on import."""
import argparse, importlib.util, os, pathlib, re, shutil, stat, subprocess
import xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).resolve().parent
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py'
spec=importlib.util.spec_from_file_location('support',SUPPORT);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
COMMON={
 'azureus-routing':'biglybtc-azureus-routing-tests',
 'owned-ddb-control':'biglybtc-owned-ddb-control-tests',
 'azureus-router-owner':'biglybtc-azureus-router-owner-tests',
 'azureus-owned-service':'biglybtc-azureus-owned-service-tests',
 'azureus-profile-io':'biglybtc-azureus-profile-io-tests',
 'i2cp-initial-channel':'biglybtc-i2cp-initial-channel-tests',
 'pure-i2cp-datagram-messages':'biglybtc-i2cp-datagram-tests',
 'i2cp-lease-backed-datagram-channel':'biglybtc-i2cp-datagram-channel-tests',
 'i2cp-bounded-datagram-receiver':'biglybtc-i2cp-receive-channel-tests',
 'i2cp-explicit-tracker-channel':'biglybtc-i2cp-tracker-channel-tests',
 'pure-i2p-streaming':'biglybtc-i2p-streaming-tests'}
GCC_UI={name:'biglybtc-'+name+'-tests' for name in ('azureus-router-binding','owned-ddb-binding','owned-lookup-tree-ui')}
PRODUCTS={'BiglyBTC':'app/BiglyBTC','BiglyBTC-headless':'app/BiglyBTC-headless','biglybtc-preview-smoke':'biglybtc-preview-smoke'}
ACTOR='biglybtc-i2p-streaming-interop-tests';ACTOR_SUITE='i2p-original-streaming-peer'
BINDING_SHA='f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e'
PROVIDER_SHA='355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443'
RECORD_SHA='623778fc3a5b25144e842a39154dab4cd107e4f115bc6474dd973f67888582d3'
JAR_SHA='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
def suites_for_lane(lane):return {**COMMON,**(GCC_UI if lane=='gcc' else {})}
def cache_values(cache):
 return {m[1]:m[2] for line in (cache/'CMakeCache.txt').read_text().splitlines() if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',line))}
def file_pins(paths):return {str(p):{'sha256':s.sha(p),'bytes':p.stat().st_size} for p in sorted(set(pathlib.Path(x).resolve() for x in paths))}
def java_pins(a):
 assert a.classes.is_absolute() and a.classes.is_dir() and a.java.is_absolute() and a.jar.is_absolute()
 classes={str(p.relative_to(a.classes)):s.sha(p) for p in a.classes.rglob('*.class')}
 assert set(classes)=={'StreamingPeer.class','StreamingPeer$Adapter.class'},'Compile the two frozen helper classes once before this batch'
 assert s.sha(a.jar)==JAR_SHA
 return {'classes':classes,'classes_directory':str(a.classes),'java':file_pins([a.java]),'jar':file_pins([a.jar])}
def provider_pins():
 binding=N/'preview/native/engine/runtime-binding/provider-mainline-routing-runtime.json';assert s.sha(binding)==BINDING_SHA
 d=s.load(binding);record=binding.parent/d['source_build_record'];assert s.sha(record)==d['source_build_record_sha256']==RECORD_SHA
 assert s.sha(d['artifact'])==PROVIDER_SHA and pathlib.Path(d['artifact']).stat().st_size==9198832
 install=pathlib.Path(d['install_root'])
 return {'binding_sha256':BINDING_SHA,'record_sha256':RECORD_SHA,'artifact_sha256':PROVIDER_SHA,
         'install_root':str(install),'installed_files_sha256':{str(p.relative_to(install)):s.sha(p) for p in sorted(install.rglob('*')) if p.is_file()}}
def dependency_pins(paths,env,out):
 records=[];libraries=set()
 for name,p in paths.items():
  command=['ldd',str(p)];r=subprocess.run(command,env=env,text=True,capture_output=True);assert r.returncode==0,(name,r.stderr)
  assert 'not found' not in r.stdout,(name,r.stdout)
  records.append({'image':name,'command':command,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
  for line in r.stdout.splitlines():
   match=re.search(r'(?:=>\s+|^\s*)(/\S+)\s+\(',line)
   if match:libraries.add(pathlib.Path(match[1]))
 s.write(out/'dependency-commands.json',records);return file_pins(libraries)
def check_xml(path,expected):
 names=s.passed_xml(path,expected);assert all(c.get('status')=='run' for c in ET.parse(path).getroot().iter('testcase'));return names
def main():
 p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);p.add_argument('--attempt',required=True)
 p.add_argument('--classes',type=pathlib.Path,required=True);p.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'))
 p.add_argument('--jar',type=pathlib.Path,default=R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar')
 p.add_argument('--actor',action='store_true',help='Execute the separately counted explicit original-manager CTest')
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 assert a.actor==(a.lane in ('clang','san')),'Clang and SAN execute the actor; GCC supplies its image for private acceptance'
 flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode),'Use jobs.mk with GNU Make4.4 --jobserver-style=fifo -j3'
 adoption_path=H/'root-adoption-r3.json';adoption=s.load(adoption_path);source=adoption['source_commit']
 cache=s.CACHES[a.lane];out=H/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False)
 env=s.environment(a.lane)
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','QT_TLS_BACKEND','LD_DEBUG_OUTPUT'):env.pop(key,None)
 assert env['MAKEFLAGS']==flags
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  path=out/key.lower();path.mkdir(mode=0o700);env[key]=str(path)
 if a.actor:env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer')
 runner=s.Runner(out,env);suites=suites_for_lane(a.lane);products=PRODUCTS if a.lane=='gcc' else {}
 targets=[*suites.values(),*products,ACTOR]
 result={'status':'FAIL','source_commit':source,'lane':a.lane,'cache':str(cache),'compiled_targets':targets,
         'extra_compiled_products':[ACTOR],'provider_rebuilt':False,'historical_passes_pooled':False,
         'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(adoption_path),'source_path_count':len(adoption['source_paths']),'jobserver_makeflags':flags}
 try:
  before=s.source_snapshot(source);s.write(out/'source-before.json',before);s.write(out/'native-source-map.json',before['files_sha256'])
  for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
  reference=java_pins(a);provider=provider_pins();result['provider']=provider;result['dist_manifest_before']=s.sha(N/'dist/BUILD_MANIFEST.json')
  cv=cache_values(cache);assert cv['CMAKE_GENERATOR']=='Ninja'
  tools=file_pins([cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/make','/usr/bin/cmake','/usr/bin/ctest',SUPPORT,pathlib.Path(__file__),H/'jobs-r3.mk'])
  versions={k:subprocess.check_output(v,env=env,text=True).splitlines()[0] for k,v in {'make':['/usr/bin/make','--version'],'ninja':[cv['CMAKE_MAKE_PROGRAM'],'--version']}.items()}
  assert re.match(r'GNU Make 4\.4(?:\.|$)',versions['make']) and re.match(r'1\.13(?:\.|$)',versions['ninja']),versions
  s.write(out/'tool-inputs.json',{'files':tools,'versions':versions,'java_reference':reference,'jobserver_fifo':fifo[1]})
  runner.run('configure',['nice','-n','5','cmake','-S',N/'preview/native','-B',cache,
      '-DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=ON','-DBIGLYBTC_I2P_STREAMING_JAVA='+str(a.java),
      '-DBIGLYBTC_I2P_STREAMING_JAR='+str(a.jar),'-DBIGLYBTC_I2P_STREAMING_CLASSES='+str(a.classes)],timeout=300)
  cache_sha=s.sha(cache/'CMakeCache.txt')
  # No -j/--parallel anywhere: Ninja1.13 consumes the shared FIFO jobserver.
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',*targets,'--','-k','0'],timeout=3600)
  s.write(out/'build-receipt.json',dict(runner.rows[-1],source_manifest_sha256=s.sha(out/'native-source-map.json')))
  paths={**{name:cache/'tests'/name for name in suites.values()},**{name:cache/path for name,path in products.items()},ACTOR:cache/'tests'/ACTOR}
  images=s.images(paths);s.write(out/'images-before.json',images);deps=dependency_pins(paths,env,out);s.write(out/'dependencies-before.json',deps)
  runner.run('inventory',['ctest','--test-dir',cache,'--show-only=json-v1'],timeout=60)
  inventory=s.load(out/'inventory.log');selected={t['name']:t for t in inventory['tests'] if t['name'] in {*suites,ACTOR_SUITE}}
  assert set(selected)=={*suites,ACTOR_SUITE}
  for name,target in {**suites,ACTOR_SUITE:ACTOR}.items():assert pathlib.Path(selected[name]['command'][0]).resolve()==paths[target].resolve(),name
  try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1800)
  finally:
   last=cache/'Testing/Temporary/LastTest.log'
   if last.exists():shutil.copyfile(last,out/'ctest-full.log')
  actual=check_xml(out/'tests.xml',suites)
  assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
  if a.actor:
   try:runner.run('actor-tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^'+ACTOR_SUITE+'$','--output-on-failure','--output-junit',out/'actor-tests.xml'],timeout=540)
   finally:
    if last.exists():shutil.copyfile(last,out/'actor-ctest-full.log')
   check_xml(out/'actor-tests.xml',[ACTOR_SUITE]);assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'actor-ctest-full.log').read_text(errors='replace'),re.M)
   result['actor_acceptance']={'status':'PASS','actual_suites':1,'suite':ACTOR_SUITE,'xml_sha256':s.sha(out/'actor-tests.xml'),'full_log_sha256':s.sha(out/'actor-ctest-full.log'),'image':images[ACTOR]}
  else:result['actor_acceptance']={'status':'COMPILE_ONLY_PRIVATE_RUN_PENDING','actual_suites':0,'image':images[ACTOR]}
  assert s.source_snapshot(source)==before and s.images(paths)==images and provider_pins()==provider
  assert file_pins(deps)==deps and file_pins(tools)==tools and java_pins(a)==reference
  assert s.sha(cache/'CMakeCache.txt')==cache_sha and s.sha(N/'dist/BUILD_MANIFEST.json')==result['dist_manifest_before']
  s.write(out/'source-after.json',s.source_snapshot(source));s.write(out/'images-after.json',s.images(paths))
  result.update(status='PASS_ROUTING_STREAMING_COHORT',suite_names=actual,actual_suites=len(actual),
      full_ctest_log_sha256=s.sha(out/'ctest-full.log'),tests_xml_sha256=s.sha(out/'tests.xml'),source_unchanged=True,images_unchanged=True,
      sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']=='PASS_ROUTING_STREAMING_COHORT' else 1
if __name__=='__main__':raise SystemExit(main())
