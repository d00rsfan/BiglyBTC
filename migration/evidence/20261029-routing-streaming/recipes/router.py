#!/usr/bin/env python3
"""Root-only supervisor; importing never builds or starts a subject."""
import argparse, importlib.util, os, pathlib, re, shutil
import xml.etree.ElementTree as ET

R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
H=pathlib.Path(__file__).resolve().parent.parent
P=R/'build/i2p-streaming-java-peer-20261029';D=P/'real-router'
T=R/'build/i2cp-isolated-router-20261029'
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py'
spec=importlib.util.spec_from_file_location('support',SUPPORT)
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
JAR=R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'
JAR_SHA='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
PARENT_SHA='537ecfe54563417a250a3e79f174d237eacfa53ac4b08abf342921c9be063cfd'
CHILD_SHA='a77d16dc7ecb725d50d9529e1cb07fc41246a89a3192aa5958dfd0097d7e517c'
ARCHIVES=('biglybtc_i2p_streaming','biglybtc_i2cp_initial_channel',
 'biglybtc_i2cp_tracker_codec','biglybtc_i2cp_leaseset2','biglybtc_i2cp_datagram',
 'biglybtc_i2cp_session_config','biglybtc_i2p_client_identity',
 'biglybtc_i2p_datagram','biglybtc_i2cp_protocol')
CLASSES={'RouterTracker.class','RouterTracker$Tracker.class','RouterStreaming.class'}
PUBLIC_ROUTER=('result.json','inputs.json','commands.json','namespace-command.json',
 'namespace-exit.json','fence-before.json','fence-after.json','router-ready.json',
 'router-result.json','tracker-ready.json','tracker-result.json','native-result.json',
 'native-lease.json','native-retired.json','stream-application.json',
 'cache-precondition.json','native.dest','tracker.dest',
 'router.log','tracker.log','native.log','namespace.log')
REQUEST=b'GET /owned-stream HTTP/1.0\r\nHost: owned.i2p\r\n\r\n'
RESPONSE_HEADER=b'HTTP/1.0 200 OK\r\nContent-Length: 8192\r\nConnection: close\r\n\r\n'

def pins(paths):
 return {str(pathlib.Path(p).resolve()):{'sha256':s.sha(p),'bytes':pathlib.Path(p).stat().st_size} for p in paths}
def cache_values(path):
 return {m[1]:m[2] for line in path.read_text().splitlines() if (m:=re.match(r'^([^#/][^:]*):[^=]+=(.*)$',line))}
def fresh(path):
 assert path.is_absolute() and not path.exists(),path
 path.mkdir(mode=0o700);return path
def class_pins(root):return {str(p.relative_to(root)):s.sha(p) for p in root.rglob('*.class')}
def public_map(paths):
 result={}
 for p in paths:
  assert p.is_file() and not p.is_symlink() and p.resolve().is_relative_to(R),p
  result[str(p.resolve())]=s.sha(p)
 return result
def remove_owned(path,root):
 if not path.exists():return
 assert path.is_dir() and not path.is_symlink() and path.parent==root and path.resolve().is_relative_to(root.resolve()),path
 shutil.rmtree(path)
def check_fences(root):
 before=s.load(root/'fence-before.json');after=s.load(root/'fence-after.json')
 for f in (before,after):
  assert f['status']=='ENFORCED_LOOPBACK_ONLY' and f['interfaces']==['lo']
  assert f['effective_capabilities']==0 and f['no_new_privileges'] is True
  assert f['network_namespace']!=f['host_namespace'] and len(f['unreachable_controls_errno'])==4
 assert before==after,'Fence changed during owned acceptance'

def main():
 p=argparse.ArgumentParser();p.add_argument('--gcc-result',type=pathlib.Path,required=True)
 p.add_argument('--source-commit',required=True);p.add_argument('--attempt',default='r1')
 p.add_argument('--producer',type=pathlib.Path,required=True)
 p.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'))
 p.add_argument('--javac',type=pathlib.Path,default=pathlib.Path('/usr/bin/javac'))
 a=p.parse_args();assert re.fullmatch('[0-9a-f]{40}',a.source_commit) and re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 assert not (H/'router-acceptance.json').exists(),'Do not overwrite an earlier completion'
 source=a.source_commit;gcc=s.load(a.gcc_result);origin=a.gcc_result.parent
 assert gcc['status']=='PASS_ROUTING_STREAMING_COHORT' and gcc['lane']=='gcc' and gcc['source_commit']==source
 assert gcc['source_unchanged'] and gcc['images_unchanged'] and gcc['runner_sha256']==s.sha(a.producer)
 assert a.producer.resolve().parent==H and re.fullmatch(r'run(?:-r\d+)?\.py',a.producer.name)
 loader=importlib.util.spec_from_file_location('batch',a.producer);batch=importlib.util.module_from_spec(loader);loader.loader.exec_module(batch)
 snapshot=s.source_snapshot(source);assert snapshot==s.load(origin/'source-before.json')==s.load(origin/'source-after.json')
 adoptions=[p for p in H.glob('root-adoption*.json') if s.sha(p)==gcc['source_receipt_sha256']];assert len(adoptions)==1
 adoption=adoptions[0];assert s.load(adoption)['source_commit']==source
 for name,row in s.load(adoption)['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 assert s.sha(origin/'tests.xml')==gcc['tests_xml_sha256'] and s.sha(origin/'ctest-full.log')==gcc['full_ctest_log_sha256']
 expected=batch.suites_for_lane('gcc');s.passed_xml(origin/'tests.xml',expected)
 assert gcc['actual_suites']==len(expected) and set(gcc['suite_names'])==set(expected)
 assert all(x.get('status')=='run' for x in ET.parse(origin/'tests.xml').getroot().iter('testcase'))
 commands=s.load(origin/'commands.json');assert s.sha(origin/'commands.json')==gcc['commands_sha256']
 assert [x['name'] for x in commands]==['configure','build','inventory','tests']
 for x in commands:assert x.get('exit')==0 and 'exception' not in x and s.sha(x['log'])==x['log_sha256']
 images=s.load(origin/'images-before.json');assert images==s.load(origin/'images-after.json')
 assert set(images)==set(gcc['compiled_targets'])
 for image in images.values():assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
 provider=batch.provider_pins();assert provider==gcc['provider']
 assert s.sha(P/'manifest.json')==PARENT_SHA
 for name,pin in s.load(P/'manifest.json')['artifacts_sha256'].items():assert s.sha(P/name)==pin,name
 child=P/'amendments/trailing-peer-stderr/manifest.json';assert s.sha(child)==CHILD_SHA
 for row in s.load(child)['effective_artifacts'].values():assert s.sha(row['path'])==row['sha256']
 assert s.sha(JAR)==JAR_SHA and a.java.resolve().parent==a.javac.resolve().parent
 cache=pathlib.Path(gcc['cache']);cv=cache_values(cache/'CMakeCache.txt');assert cv['CMAKE_GENERATOR']=='Ninja'
 archives=pins([cache/'services'/('lib'+name+'.a') for name in ARCHIVES])
 tools=pins([a.java,a.javac,cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/cmake','/usr/bin/bwrap',SUPPORT,a.producer,pathlib.Path(__file__)])
 fixtures=pins([T/'RouterTracker.java',D/'RouterStreaming.java',D/'probe.cpp',D/'CMakeLists.txt',D/'run.py',D/'source-pins.json'])
 cache_sha=s.sha(cache/'CMakeCache.txt');dist_sha=s.sha(N/'dist/BUILD_MANIFEST.json')
 parent=H/'router-runs';parent.mkdir(exist_ok=True);out=fresh(parent/a.attempt)
 run=D/'runs'/a.attempt;assert not run.exists(),'Preserve prior router attempts'
 env=s.environment('gcc')
 for key in ('MAKEFLAGS','CMAKE_BUILD_PARALLEL_LEVEL','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS'):env.pop(key,None)
 tool_dirs=[]
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory);tool_dirs.append(directory)
 runner=s.Runner(out,env);build=out/'probe-build';classes=out/'classes'
 result={'status':'FAIL','source_commit':source,'scope':'actual-isolated-router-original-manager-streaming',
   'gcc_result':{'path':str(a.gcc_result.resolve()),'sha256':s.sha(a.gcc_result)},'source_adoption':{'path':str(adoption),'sha256':s.sha(adoption)},'compiler_jobs_maximum':1,'compile_phases_serial':True,'supervisor_sha256':s.sha(__file__),
   'provider_rebuilt':False,'native_core_rebuilt':False,'historical_actor_pass_pooled':False}
 try:
  s.write(out/'source-before.json',snapshot)
  s.write(out/'inputs.json',{'source_commit':source,'gcc_result':result['gcc_result'],'provider':provider,
    'tools':tools,'fixture_sources':fixtures,'source_adoption':result['source_adoption'],'gcc_cache_sha256':cache_sha,'archives':archives,'jar_sha256':JAR_SHA})
  runner.run('java-version',[a.java,'-version'],timeout=30);runner.run('javac-version',[a.javac,'-version'],timeout=30)
  classes.mkdir(mode=0o700)
  runner.run('javac',['nice','-n','5',a.javac,'-J-Xmx128m','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','-J-Duser.home='+env['HOME'],'-J-Djava.io.tmpdir='+env['TMPDIR'],'--release','17','-cp',JAR,'-d',classes,T/'RouterTracker.java',D/'RouterStreaming.java'],timeout=180)
  classfiles=class_pins(classes);assert set(classfiles)==CLASSES
  configure=['nice','-n','5','/usr/bin/cmake','-S',D,'-B',build,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE='+cv.get('CMAKE_BUILD_TYPE','Release'),'-DCMAKE_CXX_COMPILER='+cv['CMAKE_CXX_COMPILER'],
    '-DCMAKE_MAKE_PROGRAM='+cv['CMAKE_MAKE_PROGRAM'],'-DQt6_DIR='+cv['Qt6_DIR'],
    '-DBIGLYBTC_NATIVE_SOURCE='+str(N/'preview/native'),'-DBIGLYBTC_NATIVE_BUILD='+str(cache)]
  for key in ('CMAKE_CXX_FLAGS','CMAKE_CXX_FLAGS_'+cv.get('CMAKE_BUILD_TYPE','Release').upper(),'CMAKE_EXE_LINKER_FLAGS','OPENSSL_CRYPTO_LIBRARY','OPENSSL_INCLUDE_DIR','ZLIB_LIBRARY_RELEASE','ZLIB_INCLUDE_DIR'):
   if key in cv:configure+=['-D'+key+'='+cv[key]]
  runner.run('configure',configure,timeout=120)
  runner.run('build',['nice','-n','5','/usr/bin/cmake','--build',build,'--parallel','1','--target','biglybtc-i2p-real-router-streaming-probe'],timeout=300)
  probe=build/'biglybtc-i2p-real-router-streaming-probe';probe_pin=pins([probe]);link=s.load(build/'link-inputs.json')
  assert link['archives']=={name:pin['sha256'] for name,pin in archives.items()}
  s.write(out/'compiled-inputs.json',{'source_commit':source,'classes':classfiles,'probe':probe_pin,'link_inputs':link,'probe_cache_sha256':s.sha(build/'CMakeCache.txt')})
  runner.run('router',['nice','-n','5','/usr/bin/python3','-B',D/'run.py','--native-result',a.gcc_result,'--source-commit',source,'--probe-build',build,'--classes',classes,'--jar',JAR,'--java',a.java,'--output',run,'--acknowledge-owned-signed-cache-precondition'],timeout=420)
  raw=s.load(run/'result.json');inputs=s.load(run/'inputs.json')
  assert raw['status']=='PASS_REAL_ISOLATED_ROUTER_STREAMING' and s.load(run/'namespace-exit.json')['exit_code']==0
  assert inputs['source_commit']==source and inputs['native_result']==str(a.gcc_result.resolve()) and inputs['native_result_sha256']==s.sha(a.gcc_result)
  assert inputs['archives']==link['archives'] and inputs['classes']==classfiles and inputs['probe_sha256']==s.sha(probe) and inputs['jar_sha256']==JAR_SHA
  assert inputs['java']==str(a.java.resolve()) and inputs['java_sha256']==tools[str(a.java.resolve())]['sha256']
  assert all(snapshot['files_sha256'][name]==pin for name,pin in inputs['native_source'].items())
  rows=s.load(run/'commands.json');assert [x['label'] for x in rows]==['router','tracker','native'] and all(x['exit_code']==0 for x in rows)
  assert len({x['pid'] for x in rows})==3 and all(isinstance(x['pid'],int) and x['pid']>0 for x in rows)
  check_fences(run)
  native=s.load(run/'native-result.json');peer=s.load(run/'tracker-result.json');router=s.load(run/'router-result.json');application=s.load(run/'stream-application.json')
  assert all(x['status']=='PASS' for x in (native,peer,router,application)) and raw['native']==native and raw['tracker']==peer and raw['router']==router
  assert native['body_bytes']==8192 and native['response_bytes']==len(RESPONSE_HEADER)+8192 and native['request_bytes']==len(REQUEST)
  assert native['reason']=='Exact real-router Streaming bytes and peer EOF' and int(native['transmitted_packets'])>=3 and native['general_receiver_enabled'] is False
  assert re.fullmatch('[0-9a-f]{64}',native['native_destination_sha256']) and re.fullmatch('[0-9a-f]{64}',native['tracker_destination_sha256']) and native['native_destination_sha256']!=native['tracker_destination_sha256']
  assert application['request_bytes']==len(REQUEST) and application['response_body_bytes']==8192 and application['external_i2cp'] is True
  assert peer['external_i2cp'] is True and peer['session_closed'] is True and peer['body_bytes']==8192 and router['remaining_clients']==0 and router['public_transports']==0
  assert s.load(run/'native-retired.json')['status']=='ACTUAL_NATIVE_CLIENT_RETIRED'
  assert s.sha(cache/'CMakeCache.txt')==cache_sha and pins(archives)==archives and pins(tools)==tools and pins(fixtures)==fixtures
  assert class_pins(classes)==classfiles and pins(probe_pin)==probe_pin
  assert batch.provider_pins()==provider and s.source_snapshot(source)==snapshot and s.sha(N/'dist/BUILD_MANIFEST.json')==dist_sha
  for image in images.values():assert s.sha(image['path'])==image['sha256']
  s.write(out/'source-after.json',s.source_snapshot(source))
  public=[run/name for name in PUBLIC_ROUTER]
  public += [out/name for name in ('inputs.json','compiled-inputs.json','source-before.json','source-after.json','commands.json','java-version.log','javac-version.log','javac.log','configure.log','build.log','router.log')]
  leaves=public_map(public);s.write(out/'public-evidence.json',leaves);leaves[str((out/'public-evidence.json').resolve())]=s.sha(out/'public-evidence.json')
  # Only freshly created own directories, after measured inputs/results. Never
  # delete the immutable source, qualified cache, parent run or another preview.
  for name in ('router','peer-data','home'):remove_owned(run/name,run)
  assert not (run/'native.keys').exists()
  remove_owned(build,out);remove_owned(classes,out)
  for directory in tool_dirs:remove_owned(directory,out)
  result.update(status='PASS',result_path=str(run/'result.json'),result_sha256=s.sha(run/'result.json'),
    inputs_path=str(run/'inputs.json'),inputs_sha256=s.sha(run/'inputs.json'),public_artifacts=leaves,
    source_unchanged=True,provider_unchanged=True,generated_private_directories_removed=True,
    probe_and_classes_removed_after_hashing=True,discovery_or_remote_tunnels_qualified=False)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None
  s.write(out/'result.json',result)
  if result['status']=='PASS':s.write(H/'router-acceptance.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
