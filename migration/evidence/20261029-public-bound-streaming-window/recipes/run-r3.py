#!/usr/bin/env python3
"""Root-only finite window cohort; importing performs no qualification."""
import argparse,contextlib,fcntl,hashlib,importlib.util,os,pathlib,re,shutil,stat,subprocess,time,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent
BASE=R/'build/routing-streaming-followup-20261029/run-r3.py'
EXPECTED_BASE='4c6e88b37330605cf0fbf13504317792a776b2ce84f3da8b6ed74f815c3a16c2'
assert hashlib.sha256(BASE.read_bytes()).hexdigest()==EXPECTED_BASE
assert hashlib.sha256((R/'build/mainline-scrape-20261028/recipe_support_followup.py').read_bytes()).hexdigest()=='64e0bd2b9ec87b9babf90bdf8d1235746f7dc63c797c82c5e5a138a4c2ea893a'
spec=importlib.util.spec_from_file_location('window_base',BASE);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);s=base.s
DEFAULTS={k:base.COMMON[k] for k in ('i2cp-initial-channel','i2cp-lease-backed-datagram-channel','i2cp-bounded-datagram-receiver','i2cp-explicit-tracker-channel','pure-i2p-streaming')}
ACTOR=base.ACTOR
AZUREUS={**{k:base.COMMON[k] for k in ('azureus-routing','owned-ddb-control','azureus-router-owner','azureus-owned-service','azureus-profile-io')},'azureus-maintenance':'biglybtc-azureus-maintenance-tests','azureus-database-runtime':'biglybtc-azureus-database-runtime-tests','owned-dht-bytes':'biglybtc-owned-dht-bytes-tests','azureus-public-bound':'biglybtc-azureus-public-bound-tests'}
COMMON={**AZUREUS,**DEFAULTS,'i2p-original-streaming-peer':ACTOR,'i2p-original-streaming-upload':ACTOR}
SUITES={**COMMON,**base.GCC_UI}
PUBLIC=R/'build/azureus-public-bound-20261029/amendments/bwrap-namespace/manifest.json'
PUBLIC_SHA='800a2af35264ccc77523e282b66ded9976389d0537c1f4971c8081fe98e92d66'
PUBLIC_POLICY_PARENT_SHA='122e845c0bbdd2edc499932d260b807df3c6c9c52a37251b885042c2943fde3a'
PUBLIC_REVIEW=R/'build/azureus-public-bound-20261029-review/review.json'
PUBLIC_REVIEW_SHA='803384933d6ee11eee1949e3c11ca3c912178fd074319adbf39567043fd10adb'
PUBLIC_WRAPPER=N/'preview/native/tests/azureus_public_bound_namespace.py'

def public_pins(adoption):
 assert s.sha(PUBLIC)==PUBLIC_SHA and s.sha(PUBLIC_REVIEW)==PUBLIC_REVIEW_SHA
 cap=s.load(PUBLIC);review=s.load(PUBLIC_REVIEW);assert review['status']=='PASS_SOURCE_ONLY' and review['capsule_sha256']==cap['parent_manifest_sha256']==PUBLIC_POLICY_PARENT_SHA
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(PUBLIC.parent/name)==pin
 for name,row in cap['source_paths'].items():assert adoption['source_paths'][name]['after_sha256']==row['after_sha256']==s.sha(N/name)
 for row in cap['cmake_fragments']:assert s.sha(row['path'])==row['sha256']
 return {'capsule_sha256':PUBLIC_SHA,'parent_policy_review_sha256':PUBLIC_REVIEW_SHA,'namespace_wrapper':{'path':str(PUBLIC_WRAPPER),'sha256':s.sha(PUBLIC_WRAPPER)},'namespace_tools':base.file_pins([pathlib.Path(shutil.which(name) or '/missing-required-'+name) for name in ('bwrap','ip')])}

def check_inventory(selected,images,suites):
 assert set(selected)==set(suites)
 for name,target in suites.items():
  command=selected[name]['command'];index=2 if name=='azureus-public-bound' else 0
  if index:assert len(command)>=3 and pathlib.Path(command[0]).is_absolute() and pathlib.Path(command[1]).resolve()==PUBLIC_WRAPPER.resolve()
  assert pathlib.Path(command[index]).resolve()==pathlib.Path(images[target]['path']).resolve()
 if 'azureus-public-bound' in selected:
  properties={x['name']:x['value'] for x in selected['azureus-public-bound']['properties']};assert properties['TIMEOUT']==240

PRODUCTS=base.PRODUCTS;provider_pins=base.provider_pins;PROVIDER_SHA=base.PROVIDER_SHA
STATUS='PASS_PUBLIC_BOUND_STREAMING_WINDOW_COHORT'
ACCEPTANCE=R/'build/i2p-streaming-window-20261029/acceptance/manifest.json'
ACCEPTANCE_SHA='fec3012f2abcda6ba8be92ff9ea767bbd291f1dd52019f8cb8ef441554c896dc'
REFERENCE_JAR=R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'
def suites_for_lane(lane):return SUITES if lane=='gcc' else COMMON

@contextlib.contextmanager
def subject_phase(out):
 # Different CTest invocations do not share RESOURCE_LOCK. Serialize subjects
 # across lanes, leaving configure/build outside this lock and fully parallel.
 with (W/'subject-phase.lock').open('a') as held:
  fcntl.flock(held,fcntl.LOCK_EX);row={'mechanism':'fcntl.flock','lock_path':str(W/'subject-phase.lock'),'pid':os.getpid(),'entered_monotonic_ns':time.monotonic_ns(),'scope':'subject phase only'}
  try:yield
  finally:row['left_monotonic_ns']=time.monotonic_ns();s.write(out/'subject-phase.json',row);fcntl.flock(held,fcntl.LOCK_UN)

def reference_pins(path,_depth=0):
 assert _depth<16
 d=s.load(path);assert d['status']=='PASS_PREPARED_STREAMING_WINDOW_HELPER' and d['acceptance_manifest_sha256']==ACCEPTANCE_SHA
 assert s.sha(ACCEPTANCE)==ACCEPTANCE_SHA and s.sha(d['helper_source'])==d['helper_source_sha256']==s.load(ACCEPTANCE)['helper_source']['sha256']
 if d.get('parent_reference'):
  parent=d['parent_reference'];assert s.sha(parent['path'])==parent['sha256'];original=reference_pins(pathlib.Path(parent['path']),_depth+1)
  assert d['javac_reexecuted'] is False
  for key in ('acceptance_manifest_sha256','helper_source','helper_source_sha256','classes_directory','classes','java_program','java','javac','jar','commands_sha256','runner_sha256'):assert d[key]==original[key]
  assert d['original_compile_source_commit']==original.get('original_compile_source_commit',original['source_commit']) and d['original_compile_adoption_sha256']==original.get('original_compile_adoption_sha256',original['adoption_sha256'])
 classes=pathlib.Path(d['classes_directory']);assert classes.is_absolute()
 assert {str(p.relative_to(classes)):s.sha(p) for p in classes.rglob('*.class')}==d['classes'] and set(d['classes'])=={'StreamingPeer.class','StreamingPeer$Adapter.class'}
 for group in ('java','javac','jar'):
  for name,pin in d[group].items():assert s.sha(name)==pin['sha256'] and pathlib.Path(name).stat().st_size==pin['bytes']
 assert s.sha(REFERENCE_JAR)==base.JAR_SHA
 assert s.sha(path.parent/'commands.json')==d['commands_sha256']
 for row in s.load(path.parent/'commands.json'):assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 return d

def peer_evidence(root):
 rows={};files={}
 cases=[('actual-java-peer',row,mode) for row,mode in (('http','http'),('loss-reorder','recovery'),('queue-backpressure','backpressure'),('reset','reset'),('cancel','cancel'))]
 cases += [('actual-java-peer-upload','upload-cap'+str(cap),'upload') for cap in (2,8)]
 expected_digest=hashlib.sha256(bytes((i*17+11)&255 for i in range(128*1024))).hexdigest()
 for directory,row,mode in cases:
  case=root/directory/row;app=s.load(case/'peer-application.json');peer=s.load(case/'peer-result.json');bridge=s.load(case/'bridge.json');ready=s.load(case/'peer-ready.json')
  assert app['status']==peer['status']=='PASS' and app['mode']==peer['mode']==mode and app['actual_manager_socket'] is True
  assert ready['status']=='ACTUAL_MANAGER_PROTOCOL6_LISTENER' and ready['router'] is False and peer['router_or_tunnels_claimed'] is False
  assert 0<int(peer['frames_sent'])<=4096 and 0<int(peer['frames_received'])<=4096 and not bridge['failure']
  leaves=['java-command.json','java.stderr','peer-ready.json','peer-application.json','peer-result.json','bridge.json']
  if mode=='upload':
   metrics=s.load(case/'upload-metrics.json');cap=int(row[-1]);assert metrics['status']=='PASS' and metrics['maximum_window']==cap and metrics['body_bytes']==app['upload_bytes']==131072
   assert metrics['body_sha256']==app['upload_sha256']==expected_digest and metrics['fixed_peer_frame_delay_ms']==40 and metrics['performance_parity_claimed'] is False
   assert 0<int(metrics['elapsed_microseconds']) and 0<int(metrics['body_bytes_per_second']) and int(metrics['acknowledged_bytes'])>=131072
   assert metrics['actual_maximum_in_flight']<=cap and metrics['actual_maximum_unacknowledged_data']<=cap and metrics['peak_observed_queue_bytes']<=65536
   assert (metrics['actual_distinct_data_before_first_ack']==2 if cap==2 else 3<=metrics['actual_distinct_data_before_first_ack']<=cap)
   leaves.append('upload-metrics.json');rows[row]=metrics
  for leaf in leaves:
   path=case/leaf;assert path.is_file() and not path.is_symlink();files[str(path)]={'sha256':s.sha(path),'bytes':path.stat().st_size}
 return {'rows':rows,'public_files':files,'subject_cases':len(cases)}

def verify_result(path,lane,source,adoption_path,reference_path,live=True):
 suites=suites_for_lane(lane);root=path.parent;d=s.load(path);assert d['subject_phase_sha256']==s.sha(root/'subject-phase.json');phase=s.load(root/'subject-phase.json');assert phase['scope']=='subject phase only' and phase['lock_path']==str(W/'subject-phase.lock') and phase['left_monotonic_ns']>=phase['entered_monotonic_ns'];assert d['status']==STATUS and d['source_commit']==source and d['lane']==lane
 assert d['source_receipt_sha256']==s.sha(adoption_path) and d['runner_sha256']==s.sha(__file__) and d['reference_result_sha256']==s.sha(reference_path)
 assert d['source_unchanged'] and d['images_unchanged'] and d['actual_suites']==len(suites) and set(d['suite_names'])==set(suites)
 assert s.sha(root/'tests.xml')==d['tests_xml_sha256'] and s.sha(root/'ctest-full.log')==d['full_ctest_log_sha256'];base.check_xml(root/'tests.xml',suites)
 assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(root/'ctest-full.log').read_text(errors='replace'),re.M)
 assert s.sha(root/'commands.json')==d['commands_sha256'];commands=s.load(root/'commands.json');assert [x['name'] for x in commands]==['configure','build','inventory','tests']
 for row in commands:assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 before=s.load(root/'source-before.json');assert before==s.load(root/'source-after.json') and before['source_commit']==source and s.load(root/'native-source-map.json')==before['files_sha256']
 images=s.load(root/'images-before.json');assert images==s.load(root/'images-after.json');assert set(images)==set(suites.values())|(set(PRODUCTS) if lane=='gcc' else set())==set(d['compiled_targets'])
 receipt=s.load(root/'build-receipt.json');assert receipt['exit']==0 and receipt['command']==commands[1]['command'] and receipt['source_manifest_sha256']==s.sha(root/'native-source-map.json')
 inventory={x['name']:x for x in s.load(root/'inventory.log')['tests'] if x['name'] in suites};check_inventory(inventory,images,suites)
 if live:
  assert s.source_snapshot(source)==before and d['provider']==provider_pins() and d['public_bound_inputs']==public_pins(s.load(adoption_path))
  for image in images.values():assert s.sha(image['path'])==image['sha256'] and pathlib.Path(image['path']).stat().st_size==image['bytes']
  assert reference_pins(reference_path)==s.load(root/'java-reference.json') and peer_evidence(root)==d['peer_evidence']
 return d,images,inventory

def main():
 p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);p.add_argument('--attempt',required=True);p.add_argument('--source-commit',required=True)
 for name in ('adoption','reference'):p.add_argument('--'+name,type=pathlib.Path,required=True)
 a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt) and re.fullmatch('[0-9a-f]{40}',a.source_commit)
 flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags);assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode)
 assert a.adoption.is_absolute() and a.reference.is_absolute();adoption=s.load(a.adoption);assert adoption['source_commit']==a.source_commit
 out=W/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False);cache=s.CACHES[a.lane];env=s.environment(a.lane)
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH'):env.pop(key,None)
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer');runner=s.Runner(out,env)
 suites=suites_for_lane(a.lane);targets=list(dict.fromkeys([*suites.values(),*(PRODUCTS if a.lane=='gcc' else {})]))
 result={'status':'FAIL','source_commit':a.source_commit,'lane':a.lane,'cache':str(cache),'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(a.adoption),'reference_result_sha256':s.sha(a.reference),'compiled_targets':targets,'provider_rebuilt':False,'historical_passes_pooled':False,'jobserver_makeflags':flags}
 try:
  before=s.source_snapshot(a.source_commit);s.write(out/'source-before.json',before);s.write(out/'native-source-map.json',before['files_sha256'])
  for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256']
  reference=reference_pins(a.reference);assert reference['source_commit']==a.source_commit and reference['adoption_sha256']==s.sha(a.adoption);s.write(out/'java-reference.json',reference)
  result['public_bound_inputs']=public_pins(adoption);provider=provider_pins();result['provider']=provider;distpin=s.sha(N/'dist/BUILD_MANIFEST.json');result['dist_manifest_before']=distpin
  cv=base.cache_values(cache);assert cv['CMAKE_GENERATOR']=='Ninja';tools=base.file_pins([cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/make','/usr/bin/cmake','/usr/bin/ctest',base.SUPPORT,BASE,pathlib.Path(__file__),W/'jobs-r3.mk'])
  versions={'make':subprocess.check_output(['/usr/bin/make','--version'],text=True).splitlines()[0],'ninja':subprocess.check_output([cv['CMAKE_MAKE_PROGRAM'],'--version'],text=True).strip()}
  assert re.match(r'GNU Make 4\.4(?:\.|$)',versions['make']) and re.match(r'1\.13(?:\.|$)',versions['ninja'])
  s.write(out/'tool-inputs.json',{'files':tools,'versions':versions,'jobserver_fifo':fifo[1]})
  runner.run('configure',['nice','-n','5','cmake','-S',N/'preview/native','-B',cache,'-DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=ON','-DBIGLYBTC_I2P_STREAMING_JAVA='+reference['java_program'],'-DBIGLYBTC_I2P_STREAMING_JAR='+str(REFERENCE_JAR),'-DBIGLYBTC_I2P_STREAMING_CLASSES='+reference['classes_directory']],timeout=300);cachepin=s.sha(cache/'CMakeCache.txt')
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',*targets,'--','-k','0'],timeout=3600);s.write(out/'build-receipt.json',dict(runner.rows[-1],source_manifest_sha256=s.sha(out/'native-source-map.json')))
  paths={**{t:cache/'tests'/t for t in set(suites.values())},**{t:cache/f for t,f in (PRODUCTS.items() if a.lane=='gcc' else [])}};images=s.images(paths);s.write(out/'images-before.json',images);deps=base.dependency_pins(paths,env,out);s.write(out/'dependencies-before.json',deps)
  runner.run('inventory',['ctest','--test-dir',cache,'--show-only=json-v1'],timeout=60);selected={t['name']:t for t in s.load(out/'inventory.log')['tests'] if t['name'] in suites};check_inventory(selected,images,suites)
  assert selected['i2p-original-streaming-peer']['command'][1:]==['actualOriginalManagerCarriesHttpAndRecoverablePackets','actualOriginalResetHasTypedNativeTerminal','localCancelRejectsGenuineLatePeerBytesBeforeUpdatedBarrier']
  assert selected['i2p-original-streaming-upload']['command'][1:]==['actualOriginalManagerReceives128KiBWithBoundedNativeWindow']
  for name,budget in (('i2p-original-streaming-peer',480),('i2p-original-streaming-upload',300)):
   properties={x['name']:x['value'] for x in selected[name]['properties']};assert properties['TIMEOUT']==budget
   if name=='i2p-original-streaming-upload':assert properties['ENVIRONMENT_MODIFICATION']==['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT=string_append:-upload']
  try:
   with subject_phase(out):runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=60+sum(float(next(x['value'] for x in row['properties'] if x['name']=='TIMEOUT')) for row in selected.values()))
  finally:
   last=cache/'Testing/Temporary/LastTest.log'
   if last.exists():shutil.copyfile(last,out/'ctest-full.log')
  actual=base.check_xml(out/'tests.xml',suites);assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
  assert s.source_snapshot(a.source_commit)==before and s.images(paths)==images and provider_pins()==provider and base.file_pins(deps)==deps and base.file_pins(tools)==tools and reference_pins(a.reference)==reference
  assert s.sha(cache/'CMakeCache.txt')==cachepin and s.sha(N/'dist/BUILD_MANIFEST.json')==distpin
  s.write(out/'source-after.json',s.source_snapshot(a.source_commit));s.write(out/'images-after.json',s.images(paths))
  result.update(status=STATUS,suite_names=actual,actual_suites=len(actual),subject_phase_sha256=s.sha(out/'subject-phase.json'),peer_evidence=peer_evidence(out),unique_test_images=len(set(suites.values())),tests_xml_sha256=s.sha(out/'tests.xml'),full_ctest_log_sha256=s.sha(out/'ctest-full.log'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status']==STATUS else 1
if __name__=='__main__':raise SystemExit(main())
