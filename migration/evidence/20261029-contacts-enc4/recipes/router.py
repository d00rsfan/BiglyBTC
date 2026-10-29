#!/usr/bin/env python3
"""Root-only supervisor; importing never builds or starts a subject."""
import argparse, ast, hashlib, importlib.util, os, pathlib, re, shutil
import xml.etree.ElementTree as ET

R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
H=pathlib.Path(__file__).resolve().parent
P=R/'build/i2cp-enc4-20261029';D=P/'isolated-router'
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py'
spec=importlib.util.spec_from_file_location('support',SUPPORT)
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
JAR=R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'
JAR_SHA='b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5'
CAP=P/'amendments/owned-encryption-test-column/manifest.json'
CAP_SHA='0f9487d72e2524b9f7a9fb1d58f1ea9abbca561a8a4946b6e090a1e424cdb4cf'
HELPER_CAP=P/'amendments/router-ls2-static-type/manifest.json'
HELPER_CAP_SHA='77610e1b45ccf99591f648d04b629ab01f586d1a24b432f093d099ab74bdaefd'
HELPER=HELPER_CAP.parent/'staging/RouterTracker.java'
# Read the frozen declaration without importing/running the subject recipe.
ARCHIVES=tuple(ast.literal_eval(next(n.value for n in ast.parse((D/'run.py').read_text()).body if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='ARCHIVES' for x in n.targets))))
CLASSES={'RouterTracker.class','RouterTracker$Tracker.class'}
PUBLIC_ROUTER=('result.json','inputs.json','commands.json','namespace-command.json',
 'namespace-exit.json','fence-before.json','fence-after.json','router-ready.json',
 'router-result.json','tracker-ready.json','tracker-result.json','native-result.json',
 'native-lease.json','native-retired.json',
 'cache-precondition.json','native.dest','tracker.dest',
 'router.log','tracker.log','native.log','namespace.log')

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
 p.add_argument('--producer',type=pathlib.Path,required=True);p.add_argument('--adoption',type=pathlib.Path,required=True)
 p.add_argument('--java',type=pathlib.Path,default=pathlib.Path('/usr/bin/java'))
 p.add_argument('--javac',type=pathlib.Path,default=pathlib.Path('/usr/bin/javac'))
 a=p.parse_args();assert re.fullmatch('[0-9a-f]{40}',a.source_commit) and re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 assert not (H/'router-acceptance.json').exists(),'Do not overwrite an earlier completion'
 assert all(x.is_absolute() for x in (a.gcc_result,a.producer,a.adoption,a.java,a.javac))
 source=a.source_commit;gcc=s.load(a.gcc_result);origin=a.gcc_result.parent
 assert gcc['status']=='PASS_ENC4_LEASESET_COHORT' and gcc['lane']=='gcc' and gcc['source_commit']==source
 assert gcc['source_unchanged'] and gcc['images_unchanged'] and gcc['runner_sha256']==s.sha(a.producer)
 assert a.producer.resolve().parent==H and re.fullmatch(r'run(?:-r\d+)?\.py',a.producer.name)
 loader=importlib.util.spec_from_file_location('batch',a.producer);batch=importlib.util.module_from_spec(loader);loader.loader.exec_module(batch)
 snapshot=s.source_snapshot(source);assert snapshot==s.load(origin/'source-before.json')==s.load(origin/'source-after.json')
 adoption=a.adoption;assert s.sha(adoption)==gcc['source_receipt_sha256'] and s.load(adoption)['source_commit']==source
 for name,row in s.load(adoption)['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 assert s.sha(origin/'tests.xml')==gcc['tests_xml_sha256'] and s.sha(origin/'ctest-full.log')==gcc['full_ctest_log_sha256']
 expected=batch.suites_for_lane('gcc');s.passed_xml(origin/'tests.xml',expected)
 assert gcc['actual_suites']==len(expected) and set(gcc['suite_names'])==set(expected)
 assert all(x.get('status')=='run' for x in ET.parse(origin/'tests.xml').getroot().iter('testcase'))
 commands=s.load(origin/'commands.json');assert s.sha(origin/'commands.json')==gcc['commands_sha256']
 assert [x['name'] for x in commands]==['configure','build','inventory','tests']
 for x in commands:assert x.get('exit')==0 and 'exception' not in x and s.sha(x['log'])==x['log_sha256']
 assert s.load(origin/'native-source-map.json')==snapshot['files_sha256']
 receipt=s.load(origin/'build-receipt.json');assert receipt['exit']==0 and receipt['command']==commands[1]['command'] and receipt['source_manifest_sha256']==s.sha(origin/'native-source-map.json')
 images=s.load(origin/'images-before.json');assert images==s.load(origin/'images-after.json')
 assert set(images)==set(gcc['compiled_targets'])=={*expected.values(),*batch.PRODUCTS}
 for image in images.values():assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
 provider=batch.provider_pins();assert provider==gcc['provider']
 assert s.sha(CAP)==CAP_SHA
 for row in s.load(CAP)['effective_artifacts'].values():assert s.sha(row['path'])==row['sha256']
 for name,row in s.load(CAP)['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
 assert s.sha(HELPER_CAP)==HELPER_CAP_SHA
 helper_cap=s.load(HELPER_CAP);assert helper_cap['parent_helper']=={'path':str(D/'RouterTracker.java'),'sha256':s.sha(D/'RouterTracker.java')}
 for name,pin in helper_cap['artifacts'].items():assert s.sha(HELPER_CAP.parent/name)==pin['sha256'] and (HELPER_CAP.parent/name).stat().st_size==pin['bytes']
 assert tuple(re.search(r'foreach\(name IN ITEMS\s+([^)]*)\)',(D/'CMakeLists.txt').read_text()).group(1).split())==ARCHIVES
 assert s.sha(JAR)==JAR_SHA and a.java.resolve().parent==a.javac.resolve().parent
 cache=pathlib.Path(gcc['cache']);cv=cache_values(cache/'CMakeCache.txt');assert cv['CMAKE_GENERATOR']=='Ninja'
 archives=pins([cache/'services'/('lib'+name+'.a') for name in ARCHIVES])
 tools=pins([a.java,a.javac,cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/cmake','/usr/bin/bwrap',SUPPORT,a.producer,pathlib.Path(__file__)])
 fixtures=pins([D/'RouterTracker.java',D/'probe.cpp',D/'CMakeLists.txt',D/'run.py',D/'source-pins.json',HELPER,*[HELPER_CAP.parent/name for name in helper_cap['artifacts']]])
 binding_receipts=pins([a.gcc_result,adoption,CAP,HELPER_CAP])
 cache_sha=s.sha(cache/'CMakeCache.txt');dist_sha=s.sha(N/'dist/BUILD_MANIFEST.json')
 parent=H/'router-runs';parent.mkdir(exist_ok=True);out=fresh(parent/a.attempt)
 run=D/'runs'/a.attempt;assert not run.exists(),'Preserve prior router attempts'
 env=s.environment('gcc')
 for key in ('MAKEFLAGS','CMAKE_BUILD_PARALLEL_LEVEL','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS'):env.pop(key,None)
 tool_dirs=[]
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory);tool_dirs.append(directory)
 runner=s.Runner(out,env);build=out/'probe-build';classes=out/'classes'
 result={'status':'FAIL','source_commit':source,'scope':'actual-isolated-router-enc4-warmed-local-tracker',
   'gcc_result':{'path':str(a.gcc_result.resolve()),'sha256':s.sha(a.gcc_result)},'source_adoption':{'path':str(adoption),'sha256':s.sha(adoption)},'compiler_jobs_maximum':1,'compile_phases_serial':True,'supervisor_sha256':s.sha(__file__),
   'provider_rebuilt':False,'native_core_rebuilt':False,'old_java_or_actor_pass_pooled':False,'java_helper_capsule':{'path':str(HELPER_CAP),'sha256':HELPER_CAP_SHA},'compiled_java_source':{'path':str(HELPER),'sha256':s.sha(HELPER)}}
 try:
  s.write(out/'source-before.json',snapshot)
  s.write(out/'inputs.json',{'source_commit':source,'gcc_result':result['gcc_result'],'provider':provider,
    'tools':tools,'fixture_sources':fixtures,'binding_receipts':binding_receipts,'source_adoption':result['source_adoption'],'gcc_cache_sha256':cache_sha,'archives':archives,'jar_sha256':JAR_SHA,'archive_count':len(archives),'enc4_capsule_sha256':CAP_SHA,'java_helper_capsule':result['java_helper_capsule'],'compiled_java_source':result['compiled_java_source']})
  runner.run('java-version',[a.java,'-version'],timeout=30);runner.run('javac-version',[a.javac,'-version'],timeout=30)
  classes.mkdir(mode=0o700)
  runner.run('javac',['nice','-n','5',a.javac,'-J-Xmx128m','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','-J-Duser.home='+env['HOME'],'-J-Djava.io.tmpdir='+env['TMPDIR'],'--release','17','-cp',JAR,'-d',classes,HELPER],timeout=180)
  classfiles=class_pins(classes);assert set(classfiles)==CLASSES
  configure=['nice','-n','5','/usr/bin/cmake','-S',D,'-B',build,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE='+cv.get('CMAKE_BUILD_TYPE','Release'),'-DCMAKE_CXX_COMPILER='+cv['CMAKE_CXX_COMPILER'],
    '-DCMAKE_MAKE_PROGRAM='+cv['CMAKE_MAKE_PROGRAM'],'-DQt6_DIR='+cv['Qt6_DIR'],
    '-DBIGLYBTC_NATIVE_SOURCE='+str(N/'preview/native'),'-DBIGLYBTC_NATIVE_BUILD='+str(cache)]
  for key in ('CMAKE_CXX_FLAGS','CMAKE_CXX_FLAGS_'+cv.get('CMAKE_BUILD_TYPE','Release').upper(),'CMAKE_EXE_LINKER_FLAGS','OPENSSL_CRYPTO_LIBRARY','OPENSSL_INCLUDE_DIR','ZLIB_LIBRARY_RELEASE','ZLIB_INCLUDE_DIR'):
   if key in cv:configure+=['-D'+key+'='+cv[key]]
  runner.run('configure',configure,timeout=120)
  runner.run('build',['nice','-n','5','/usr/bin/cmake','--build',build,'--parallel','1','--target','biglybtc-i2cp-enc4-router-probe'],timeout=300)
  probe=build/'biglybtc-i2cp-enc4-router-probe';probe_pin=pins([probe]);link=s.load(build/'link-inputs.json')
  assert link['archives']=={name:pin['sha256'] for name,pin in archives.items()}
  s.write(out/'compiled-inputs.json',{'source_commit':source,'classes':classfiles,'compiled_java_source':result['compiled_java_source'],'probe':probe_pin,'link_inputs':link,'probe_cache_sha256':s.sha(build/'CMakeCache.txt')})
  runner.run('router',['nice','-n','5','/usr/bin/python3','-B',D/'run.py','--native-result',a.gcc_result,'--source-commit',source,'--probe-build',build,'--classes',classes,'--jar',JAR,'--java',a.java,'--output',run,'--acknowledge-owned-signed-cache-precondition'],timeout=420)
  raw=s.load(run/'result.json');inputs=s.load(run/'inputs.json')
  assert raw['status']=='PASS_ENC4_WARMED_LOCAL_LOOKUP_AND_REAL_ROUTER_TRACKER' and s.load(run/'namespace-exit.json')['exit_code']==0
  assert inputs['source_commit']==source and inputs['native_result']==str(a.gcc_result.resolve()) and inputs['native_result_sha256']==s.sha(a.gcc_result)
  assert inputs['archives']==link['archives'] and inputs['classes']==classfiles and inputs['probe_sha256']==s.sha(probe) and inputs['jar_sha256']==JAR_SHA
  assert inputs['java']==str(a.java.resolve()) and inputs['java_sha256']==tools[str(a.java.resolve())]['sha256']
  assert all(snapshot['files_sha256'][name]==pin for name,pin in inputs['native_source'].items())
  rows=s.load(run/'commands.json');assert [x['label'] for x in rows]==['router','tracker','native'] and all(x['exit_code']==0 for x in rows)
  assert len({x['pid'] for x in rows})==3 and all(isinstance(x['pid'],int) and x['pid']>0 for x in rows)
  check_fences(run)
  native=s.load(run/'native-result.json');peer=s.load(run/'tracker-result.json');router=s.load(run/'router-result.json');marker=s.load(run/'cache-precondition.json')
  assert all(x['status']=='PASS' for x in (native,peer,router)) and raw['native']==native and raw['tracker']==peer and raw['router']==router
  assert (native['seeds'],native['downloaded'],native['peers'])==(3,4,5) and native['general_receiver_enabled'] is False
  assert native['reason']=='Actual original router carried scoped signed2/claimed3 and RAW18 counts; native TCP closed'
  assert marker['status']=='ACTUAL_SIGNED_LS2_CACHE_PRECONDITION' and marker['signature_verified'] is True and marker['zero_hop_pools_checked'] is True
  for family in ('native','tracker'):
   identity=run/(family+'.dest');assert 387<=identity.stat().st_size<=1024
   digest=hashlib.sha256(identity.read_bytes()).hexdigest();assert digest==native[family+'_destination_sha256']==marker[family+'_destination_sha256']
  assert native['native_destination_sha256']!=native['tracker_destination_sha256']
  assert s.load(run/'native-lease.json')['destination_sha256']==native['native_destination_sha256']
  assert peer['external_i2cp'] is True and (peer['signed_connects'],peer['claimed_scrapes'],peer['raw_replies'])==(1,1,2)
  assert router['remaining_clients']==0 and router['public_transports']==0 and router['scope']=='enc4-warmed-local-cache-and-real-local-client-forwarding'
  # The pinned Java accepted() predicate checks signed current LS2 type3 with
  # exactly one type4/32-byte key for BOTH unchanged Destinations before marker.
  assert s.load(run/'native-retired.json')['status']=='ACTUAL_NATIVE_CLIENT_RETIRED'
  assert s.sha(cache/'CMakeCache.txt')==cache_sha and pins(archives)==archives and pins(tools)==tools and pins(fixtures)==fixtures and pins(binding_receipts)==binding_receipts
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
    probe_and_classes_removed_after_hashing=True,actual_imported_archives=len(archives),both_clients_actual_accepted_ls2_key_type=4,lease_key_bytes=32,discovery_or_remote_tunnels_qualified=False)
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None
  s.write(out/'result.json',result)
  if result['status']=='PASS':s.write(H/'router-acceptance.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
