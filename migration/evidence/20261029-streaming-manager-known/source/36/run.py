#!/usr/bin/env python3
"""Root-only affected M rows plus explicit source/image associations; not a fresh full cohort."""
import argparse,copy,fcntl,hashlib,importlib.util,os,pathlib,re,shutil,stat,subprocess,time,xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';W=pathlib.Path(__file__).resolve().parent;D=R/'build/manager-known-followup-20261029/diagnostics'
HELPER=D/'two-selectors/amendments/java-asynchronous-causes/run.py'
assert hashlib.sha256(HELPER.read_bytes()).hexdigest()=='762d6ebe18493756042607cf93fb021003ca82c7c5789600a0e7b20e0b5b3bf4'
spec=importlib.util.spec_from_file_location('diagnostic_source',HELPER);d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d);b=d.b;base=d.base;s=d.s;P=d.P
D2=HELPER.parent/'runs/san-d2';REFERENCE=d.REFERENCE
PEER='i2p-original-streaming-peer';UPLOAD='i2p-original-streaming-upload';MANAGER='i2p-original-streaming-manager';PUBLIC='azureus-public-bound'
MODE='CURRENT_M_SOURCE_IMAGE_ASSOCIATION_WITH_FRESH_AFFECTED_SUITES'

def ref(path):return {'path':str(path),'sha256':s.sha(path)}
def pins():
 cap=s.load(W/'manifest.json')
 for name,pin in cap['artifacts_sha256'].items():assert s.sha(W/name)==pin,name
 for name,pin in cap['inputs_sha256'].items():assert s.sha(name)==pin,name
 return cap

def source_guard(source,adoption):
 assert adoption['status']=='EFFECTIVE_M_SOURCE_ASSOCIATION_ONLY' and adoption['source_commit']==source
 old=s.load(P/'root-adoption.json');diag=s.load(d.ADOPTION);assert adoption['original_m_adoption']==ref(P/'root-adoption.json') and adoption['diagnostic_adoption']==ref(d.ADOPTION)
 expected=copy.deepcopy(old['source_paths'])
 for name,row in diag['source_paths'].items():
  if name not in expected:expected[name]=row
  else:expected[name].update(after_path=row['after_path'],after_sha256=row['after_sha256'])
 assert adoption['source_paths']==expected and len(expected)==25
 for name,row in expected.items():assert s.sha(N/name)==s.sha(row['after_path'])==row['after_sha256']
 return d.source_guard(source,diag)

def xml_rows(path):
 cases=list(ET.parse(path).getroot().iter('testcase'));rows={c.get('name'):c for c in cases};assert cases and len(rows)==len(cases);return rows

def passed(row):return row.get('status','run')=='run' and not any(row.find(x) is not None for x in ('failure','error','skipped'))
def row_ref(path,row):return {**ref(path),'case_name':row.get('name'),'case_sha256':hashlib.sha256(ET.tostring(row,encoding='utf-8')).hexdigest()}

def d2_manager(images,current):
 result=s.load(D2/'result.json');assert result['status']=='DIAGNOSTIC_COMPLETED_SUBJECT_PASS' and result['source_commit']==current['source_commit'] and result['qualification'] is False and result['source_unchanged'] and result['images_unchanged'] and result['dependencies_unchanged'] and result['tools_unchanged']
 assert result['runner_sha256']==s.sha(HELPER) and result['adoption_sha256']==s.sha(d.ADOPTION) and result['java_helper_origin']==ref(REFERENCE) and result['javac_invocations']==0
 for name,pin in result['artifacts_sha256'].items():assert s.sha(D2/name)==pin,name
 assert s.load(D2/'source-before.json')==s.load(D2/'source-after.json')==current
 previous=s.load(D2/'images-before.json');assert previous==s.load(D2/'images-after.json') and images[b.MANAGER]==previous[b.MANAGER]
 for leaf in ('dependencies-before.json','tool-inputs.json'):values=s.load(D2/leaf);assert base.file_pins(values)==values
 commands=s.load(D2/'commands.json');assert s.sha(D2/'commands.json')==result['commands_sha256'] and [r['name'] for r in commands]==['configure','build','selected']
 for row in commands:assert row['exit']==0 and 'exception' not in row and s.sha(row['log'])==row['log_sha256']
 assert commands[-1]['command']==['nice','-n','5',images[b.MANAGER]['path'],d.CASES['san'][2],'-o',str(D2/'selected.txt')+',txt','-o',str(D2/'selected.xml')+',junitxml']
 root=ET.parse(D2/'selected.xml').getroot();rows=xml_rows(D2/'selected.xml');assert root.get('tests')=='3' and all(root.get(k)=='0' for k in ('failures','errors','skipped'))
 assert set(rows)=={'initTestCase',d.CASES['san'][2],'cleanupTestCase'} and all(passed(row) for row in rows.values())
 assert b.provider_pins()==result['provider'] and d.reference_pins()==s.load(D2/'java-reference.json') and b.protected_original()==result['original_head_index_unchanged']
 return {'mode':'current_d2_full_manager_method','result':ref(D2/'result.json'),'xml':ref(D2/'selected.xml'),'actual_qt_cases':list(rows),'image':images[b.MANAGER],'evidence':b.manager_evidence(D2)}

def associate(lane,fresh,images,inventory,current):
 suites=b.suites_for_lane(lane);oldroot=P/'runs'/(lane+'-r2');old=s.load(oldroot/'result.json');original_images=s.load(oldroot/'images-before.json');oldrows=xml_rows(oldroot/'tests.xml');assert set(oldrows)==set(suites) and old['source_commit']==d.BASIS
 for name in ('commands.json','tests.xml','ctest-full.log','source-before.json','images-before.json','tool-inputs.json','dependencies-before.json'):assert (oldroot/name).is_file()
 assert s.sha(oldroot/'commands.json')==old['commands_sha256'];assert all(row.find('skipped') is None and row.find('error') is None for row in oldrows.values())
 for leaf in ('dependencies-before.json','tool-inputs.json'):
  values=s.load(oldroot/leaf);values=values['files'] if leaf=='tool-inputs.json' else values;assert base.file_pins(values)==values
 commands=s.load(oldroot/'commands.json');assert [x['name'] for x in commands]==['configure','build','inventory','tests'] and all(x['exit']==0 for x in commands[:3])
 assert old['status']==('PASS_STREAMING_MANAGER_KNOWN_COHORT' if lane=='clang' else 'FAIL') and commands[-1]['exit']==(0 if lane=='clang' else 8)
 for row in commands:assert s.sha(row['log'])==row['log_sha256']
 oldinventory={row['name']:row for row in s.load(oldroot/'inventory.log')['tests']};freshrows=xml_rows(fresh);expected_fresh={PEER,UPLOAD}|({MANAGER} if lane!='san' else set());assert set(freshrows)==expected_fresh
 associations={};retained=0
 for suite,target in suites.items():
  image=images[target]
  if suite in freshrows:associations[suite]={'mode':'fresh_current_ctest','outcome':'PASS' if passed(freshrows[suite]) else 'FAIL','origin':row_ref(fresh,freshrows[suite]),'image':image};continue
  if suite==MANAGER:assert lane=='san';associations[suite]=dict(d2_manager(images,current),outcome='PASS');continue
  assert image==original_images[target],suite
  before=copy.deepcopy(oldinventory[suite]);after=copy.deepcopy(inventory[suite]);oldproperties={x['name']:x['value'] for x in before['properties']};newproperties={x['name']:x['value'] for x in after['properties']}
  if suite==PUBLIC:assert oldproperties['TIMEOUT']==240 and newproperties['TIMEOUT']==1000;oldproperties['TIMEOUT']=1000
  assert before['command']==after['command'] and oldproperties==newproperties,suite
  if lane=='gcc' and suite==PUBLIC:
   blocked=s.load(D/'public-recovery-r1/result.json');assert blocked['status']=='FAIL' and blocked['source_commit']==current['source_commit'] and blocked['builds_executed'] is False and not passed(oldrows[suite])
   for name,pin in blocked['artifacts_sha256'].items():assert s.sha(D/'public-recovery-r1'/name)==pin
   refusal=s.load(D/'public-recovery-r1/namespace-admin.json');assert refusal['status']=='CANCELLED_OR_BOUNDED_LAUNCH_EXPIRED' and refusal['output_bytes']==[0,0] and refusal['verified_drop_marker'] is False
   associations[suite]={'mode':'blocked_public_auth_no_credit','outcome':'BLOCKED','original_failed_origin':row_ref(oldroot/'tests.xml',oldrows[suite]),'later_failed_attempt':ref(D/'public-recovery-r1/result.json'),'image':image};continue
  assert passed(oldrows[suite]),suite;retained+=1
  associations[suite]={'mode':'unchanged_current_r2_pass','outcome':'PASS','origin':row_ref(oldroot/'tests.xml',oldrows[suite]),'original_whole_invocation_status':old['status'],'image':image,'definition_unchanged_except_public_outer_budget':True}
 assert retained==(18 if lane=='gcc' else 16) and len(associations)==len(suites)
 return associations

def main():
 parser=argparse.ArgumentParser();parser.add_argument('lane',choices=['gcc','clang','san']);parser.add_argument('--attempt',required=True);parser.add_argument('--source-commit',required=True);parser.add_argument('--adoption',type=pathlib.Path,required=True);a=parser.parse_args()
 assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt) and a.adoption.resolve()==(W/'effective-adoption.json').resolve();pins();flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode)
 adoption=s.load(a.adoption);out=W/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False);cache=s.CACHES[a.lane];env=s.environment(a.lane)
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer');env['BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT']=str(out/'actual-manager');runner=s.Runner(out,env)
 suites=b.suites_for_lane(a.lane);fresh={PEER:b.ACTOR,UPLOAD:b.ACTOR};fresh.update({MANAGER:b.MANAGER} if a.lane!='san' else {});targets=[b.ACTOR,b.MANAGER]
 result={'status':'FAILED_CURRENT_M_ASSOCIATION','qualification_mode':MODE,'source_commit':a.source_commit,'lane':a.lane,'compiled_targets':targets,'fresh_suites':list(fresh),'fresh_full_cohort':False,'new_javac_invocations':0,'java_preparation':ref(REFERENCE),'effective_adoption':ref(a.adoption),'runner_sha256':s.sha(__file__),'original_failed_invocations_preserved':True,'historical_different_source_passes_pooled':False,'intermittent_syn_and_close_cause':'UNRESOLVED; no stable/fixed/performance-parity claim'}
 try:
  before=source_guard(a.source_commit,adoption);protected=b.protected_original();assert protected==adoption['original_head_index_unchanged'];reference=d.reference_pins();provider=b.provider_pins();dist=s.sha(N/'dist/BUILD_MANIFEST.json');s.write(out/'source-before.json',before);s.write(out/'java-reference.json',reference);s.write(out/'native-source-map.json',before['files_sha256'])
  original=s.load(P/'runs'/(a.lane+'-r2')/'images-before.json')
  if a.lane=='gcc':original.update(s.load(D/'two-selectors/runs/gcc-d1/images-before.json'))
  if a.lane=='san':original.update(s.load(D2/'images-before.json'))
  allpaths={name:pathlib.Path(row['path']) for name,row in original.items()};assert s.images(allpaths)==original;s.write(out/'images-before-build.json',original)
  olddeps=s.load(P/'runs'/(a.lane+'-r2')/'dependencies-before.json');assert base.file_pins(olddeps)==olddeps;cv=base.cache_values(cache);tools=base.file_pins([cv['CMAKE_C_COMPILER'],cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/make','/usr/bin/cmake','/usr/bin/ctest',W/'run.py',W/'jobs.mk',HELPER]);s.write(out/'tool-inputs.json',tools)
  assert re.match(r'GNU Make 4\.4(?:\.|$)',subprocess.check_output(['/usr/bin/make','--version'],text=True).splitlines()[0]) and re.match(r'1\.13(?:\.|$)',subprocess.check_output([cv['CMAKE_MAKE_PROGRAM'],'--version'],text=True).strip())
  runner.run('configure',['nice','-n','5','cmake','-S',N/'preview/native','-B',cache,'-DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=ON','-DBIGLYBTC_I2P_STREAMING_JAVA='+reference['java_program'],'-DBIGLYBTC_I2P_STREAMING_JAR='+str(b.REFERENCE_JAR),'-DBIGLYBTC_I2P_STREAMING_CLASSES='+reference['legacy_classes'],'-DBIGLYBTC_I2P_STREAMING_MANAGER_JAVA_ACCEPTANCE=ON','-DBIGLYBTC_I2P_MANAGER_JAVA='+reference['java_program'],'-DBIGLYBTC_I2P_MANAGER_JAR='+str(b.REFERENCE_JAR),'-DBIGLYBTC_I2P_MANAGER_CLASSES='+reference['manager_classes']],timeout=300);cachepin=s.sha(cache/'CMakeCache.txt')
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',*targets,'--','-k','0'],timeout=2400);s.write(out/'build-receipt.json',dict(runner.rows[-1],source_manifest_sha256=s.sha(out/'native-source-map.json')))
  images=s.images(allpaths);assert all(images[name]==row for name,row in original.items() if name not in targets);s.write(out/'images-before.json',images);deps=base.dependency_pins({t:allpaths[t] for t in targets},env,out);s.write(out/'dependencies-before.json',deps)
  runner.run('inventory',['ctest','--test-dir',cache,'--show-only=json-v1'],timeout=60);inventory={row['name']:row for row in s.load(out/'inventory.log')['tests'] if row['name'] in suites};assert set(inventory)==set(suites)
  for name,target in suites.items():
   command=inventory[name]['command'];index=2 if name==PUBLIC else 0;assert pathlib.Path(command[index]).resolve()==pathlib.Path(images[target]['path']).resolve()
   if index:assert pathlib.Path(command[1]).resolve()==b.PUBLIC_WRAPPER.resolve() and next(x['value'] for x in inventory[name]['properties'] if x['name']=='TIMEOUT')==1000
  assert inventory[MANAGER]['command']==[images[b.MANAGER]['path']]
  with (W/'subject-phase.lock').open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX);phase={'scope':'affected suites only; no Public; builds outside','entered_monotonic_ns':time.monotonic_ns(),'pid':os.getpid()}
   try:
    try:runner.run('fresh-tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(map(re.escape,fresh))+')$','--output-on-failure','--output-junit',out/'fresh-tests.xml'],timeout=60+sum(float(next(x['value'] for x in inventory[name]['properties'] if x['name']=='TIMEOUT')) for name in fresh))
    except BaseException as exc:result['fresh_execution_error']=repr(exc)
   finally:
    phase['left_monotonic_ns']=time.monotonic_ns();s.write(out/'subject-phase.json',phase);fcntl.flock(lock,fcntl.LOCK_UN);last=cache/'Testing/Temporary/LastTest.log'
    if last.exists():shutil.copyfile(last,out/'fresh-ctest-full.log')
  assert source_guard(a.source_commit,adoption)==before and b.protected_original()==protected and d.reference_pins()==reference and b.provider_pins()==provider and s.images(allpaths)==images and base.file_pins(deps)==deps and base.file_pins(olddeps)==olddeps and base.file_pins(tools)==tools and s.sha(cache/'CMakeCache.txt')==cachepin and s.sha(N/'dist/BUILD_MANIFEST.json')==dist
  s.write(out/'source-after.json',source_guard(a.source_commit,adoption));s.write(out/'images-after.json',s.images(allpaths));s.write(out/'dependencies-after.json',base.file_pins(deps));s.write(out/'tools-after.json',base.file_pins(tools))
  associations=associate(a.lane,out/'fresh-tests.xml',images,inventory,before);s.write(out/'associated-suites.json',associations);failures=[name for name,row in associations.items() if row['outcome']=='FAIL'];blocked=[name for name,row in associations.items() if row['outcome']=='BLOCKED']
  fresh_ok='fresh_execution_error' not in result and not failures
  result.update(status=('ASSOCIATED_CURRENT_M_BLOCKED_PUBLIC' if blocked else 'PASS_ASSOCIATED_CURRENT_M_ROWS') if fresh_ok else 'FAILED_FRESH_AFFECTED_SUITES',fresh_actual_suites=len(fresh),retained_r2_passed_suites=sum(row['mode']=='unchanged_current_r2_pass' for row in associations.values()),reused_current_diagnostic_suites=sum(row['mode']=='current_d2_full_manager_method' for row in associations.values()),blocked_suites=blocked,associated_total_suites=len(associations),association=ref(out/'associated-suites.json'),source_unchanged=True,images_unchanged=True,dependencies_unchanged=True,original_head_index_unchanged=protected,provider=provider,peer_evidence=b.peer_evidence(out) if fresh_ok else None,manager_evidence=(b.manager_evidence(D2) if a.lane=='san' else b.manager_evidence(out)) if fresh_ok else None)
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;result['artifacts_sha256']={str(p.relative_to(out)):s.sha(p) for p in out.rglob('*') if p.is_file() and p.stat().st_size<=10*1024*1024 and (p.parent==out or p.name in ('packet-metadata-tail.json','bridge.json','java.stderr','java-command.json','peer-result.json','peer-application.json','native-acceptance.json','http-9000.json','http-9001.json','inbound-application.json','cancel-application.json','owned-logger.config','owned-streaming-0.log','owned-streaming-1.log')) and p.suffix not in ('.frames','.class','.jar')};s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'] in ('PASS_ASSOCIATED_CURRENT_M_ROWS','ASSOCIATED_CURRENT_M_BLOCKED_PUBLIC') else 1
if __name__=='__main__':raise SystemExit(main())
