#!/usr/bin/env python3
"""Root-run affected optional-owner checks; no work on import."""
import argparse,fcntl,hashlib,importlib.util,os,pathlib,re,shutil,stat,subprocess,time,xml.etree.ElementTree as ET
W=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('native_adoption',W/'adopt.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a);R=a.R;N=a.N
SUPPORT=R/'build/mainline-scrape-20261028/recipe_support_followup.py';BASE=R/'build/routing-streaming-followup-20261029/run-r3.py'
assert a.sha(SUPPORT)=='64e0bd2b9ec87b9babf90bdf8d1235746f7dc63c797c82c5e5a138a4c2ea893a' and a.sha(BASE)=='4c6e88b37330605cf0fbf13504317792a776b2ce84f3da8b6ed74f815c3a16c2'
spec=importlib.util.spec_from_file_location('native_tools',BASE);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);s=base.s
OWNER='biglybtc_i2p_provider_owner';PROBE='biglybtc-i2p-provider-transfer-probe';STATUS='PASS_PRIVATE_I2P_OWNER_AFFECTED_COHORT';DEFAULT_STATUS='PASS_DEFAULT_I2P_ATTACHMENT_CONTROLS'
CORE={'pure-owned-sam-protocol':'biglybtc-owned-sam-protocol-tests','owned-i2p-profile-source':'biglybtc-owned-i2p-source-tests','network-policy':'biglybtc-network-policy-tests','managed-storage':'biglybtc-managed-storage-tests'}
SUITES={**CORE,'i2cp-initial-channel':'biglybtc-i2cp-initial-channel-tests','i2cp-lease-backed-datagram-channel':'biglybtc-i2cp-datagram-channel-tests','i2cp-bounded-datagram-receiver':'biglybtc-i2cp-receive-channel-tests','i2cp-explicit-tracker-channel':'biglybtc-i2cp-tracker-channel-tests','i2cp-bounded-stream-manager':'biglybtc-i2cp-stream-manager-tests','pure-i2p-streaming':'biglybtc-i2p-streaming-tests','pure-i2p-streaming-manager':'biglybtc-i2p-streaming-manager-tests'}

def provider(args):
 approval=a.load(args.provider_approval);record=a.load(args.provider_record);runtime=a.load(args.provider_runtime_manifest);candidate=args.provider_record.parent
 assert a.sha(args.provider_approval)==args.provider_approval_sha256 and approval['schema']=='BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1'
 assert approval['source_capsule_sha256']==record['source_capsule_sha256']=='8f12f51c1426d4de59c9e9df6f9c2cb767001fe2574b6bc6668b8ce4e6722dc9'
 assert record['schema']=='BIGLYBTC_PRIVATE_I2P_OWNER_SOURCE_RECORD_V1' and record['status']=='BUILT_DIRECTLY_TESTED_NOT_APPROVED' and record['native_runtime_approved'] is False and record['shipping_approved'] is False
 assert approval['source_build_record_sha256']==runtime['source_build_record_sha256']==a.sha(args.provider_record) and approval['runtime_manifest_sha256']==a.sha(args.provider_runtime_manifest) and approval['review_sha256']==a.sha(args.provider_review)
 assert pathlib.Path(runtime['source_build_record']).resolve()==args.provider_record.resolve() and runtime['native_runtime_approved'] is False
 artifact=pathlib.Path(record['artifact']);install=pathlib.Path(record['install_root']);sdk=install/'include'
 assert candidate.is_dir() and not candidate.is_symlink() and candidate.resolve()==candidate and candidate.parent==a.PARENT.parent and re.fullmatch(r'provider-candidate-[a-z0-9-]+',candidate.name) and install==candidate/'install' and artifact==install/'lib/libtorrent-rasterbar.so.2.0.12' and not artifact.is_symlink()
 assert a.sha(artifact)==approval['artifact_sha256']==record['artifact_sha256']==runtime['artifact_sha256'] and artifact.stat().st_size==approval['artifact_bytes']==record['artifact_bytes']==runtime['artifact_bytes']
 installed={str(p.relative_to(install)):a.sha(p) for p in install.rglob('*') if p.is_file()};assert installed==record['installed_sha256']
 headers={str(p.relative_to(sdk)):a.sha(p) for p in (sdk/'libtorrent').rglob('*') if p.is_file()};assert headers==approval['headers_sha256']==record['headers_sha256'] and all(not (sdk/name).is_symlink() for name in headers)
 assert hashlib.sha256(__import__('json').dumps(headers,sort_keys=True,separators=(',',':')).encode()).hexdigest()==approval['sdk_tree_sha256']==record['sdk_tree_sha256']
 assert len(set(record['required_exports']))==31 and record['actual_controls']['status']=='ACTUAL_PRIVATE_I2P_PROVIDER_DIRECT_CONTROLS_PASS_NOT_APPROVED' and record['actual_controls']['actual_ctest_suites']==9
 for name,pin in record['evidence_sha256'].items():assert a.sha(candidate/name)==pin,name
 for name,pin in record['reviewed_inputs_sha256'].items():assert a.sha(name)==pin,name
 source=pathlib.Path(record['source_root']);assert {str(p.relative_to(source)):a.sha(p) for p in source.rglob('*') if p.is_file()}==record['source_inputs_sha256']
 return {'record':a.ref(args.provider_record),'approval':a.ref(args.provider_approval),'runtime_manifest':a.ref(args.provider_runtime_manifest),'review':a.ref(args.provider_review),'artifact':a.ref(artifact),'artifact_bytes':artifact.stat().st_size,'install_root':str(install),'sdk_tree_sha256':approval['sdk_tree_sha256'],'headers_sha256':headers,'required_exports':record['required_exports'],'shipping_approved':False}

def main():
 parser=argparse.ArgumentParser();parser.add_argument('lane',choices=('default','gcc','clang','san'));parser.add_argument('--attempt',required=True);parser.add_argument('--source-commit',required=True);parser.add_argument('--adoption',type=pathlib.Path,required=True)
 for name in ('provider-record','provider-approval','provider-runtime-manifest','provider-review'):parser.add_argument('--'+name,type=pathlib.Path,required=True)
 parser.add_argument('--provider-approval-sha256',required=True);args=parser.parse_args()
 assert re.fullmatch('[A-Za-z0-9_-]+',args.attempt) and args.adoption.resolve()==(W/'root-adoption.json').resolve()
 flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags);assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode)
 a.pins();adoption=a.load(args.adoption);before=a.source_guard(args.source_commit,adoption);protected=a.protected();oldshipping=a.shipping();assert protected==adoption['original_head_index_unchanged']
 private=args.lane!='default';approved=provider(args);cache=a.PARENT/'caches'/args.lane if private else s.CACHES['gcc'];out=W/'runs'/(args.lane+'-'+args.attempt);out.mkdir(parents=True,exist_ok=False)
 if private:
  if args.lane=='clang':a.resume_cache(cache,approved,before)
  else:assert not cache.exists()
  cache.parent.mkdir(exist_ok=True)
 env=s.environment('san' if args.lane=='san' else None)
 for key in ('CMAKE_BUILD_PARALLEL_LEVEL','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS','CLASSPATH','LSAN_OPTIONS','ASAN_OPTIONS','UBSAN_OPTIONS','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','LD_DEBUG_OUTPUT','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_I2P_MANAGER_ACCEPTANCE_ROOT','QT_TLS_BACKEND','BIGLYBTC_PUBLIC_ADMIN_MODE','BIGLYBTC_PUBLIC_ADMIN_HELPER','BIGLYBTC_PUBLIC_ADMIN_SHA256','BIGLYBTC_PUBLIC_ADMIN_RECEIPT'):env.pop(key,None)
 if args.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 for key in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR','TMPDIR'):directory=out/key.lower();directory.mkdir(mode=0o700);env[key]=str(directory)
 suites=SUITES if private else CORE;targets=list(suites.values())+([OWNER]+([PROBE] if args.lane=='gcc' else []) if private else ['BiglyBTC']);runner=s.Runner(out,env)
 result={'status':'FAIL','source_commit':args.source_commit,'lane':args.lane,'cache':str(cache),'runner_sha256':a.sha(__file__),'adoption':a.ref(args.adoption),'private_owner_enabled':private,'old_shipping_provider':oldshipping,'provider':approved,'compiled_targets':targets,'suite_names':list(suites),'java_invocations':0,'actual_transfer':False,'default_or_public_activation':False,'stop_after_this_section':True,'failed_clang_origin':a.ref(a.PARENT/'runs/clang-r1/result.json') if args.lane=='clang' else None}
 try:
  a.write(out/'source-before.json',before);a.write(out/'native-source-map.json',before['files_sha256']);dist=a.sha(N/'dist/BUILD_MANIFEST.json');a.write(out/'dist-before.json',{'manifest_sha256':dist})
  command=['nice','-n','5','cmake','-S',N/'preview/native','-B',cache]
  if private:
   install=pathlib.Path(approved['install_root']);prefix=';'.join(map(str,[install,N/'build/dependencies/sysroot/usr',N/'build/dependencies/country/sysroot/usr','/opt/Qt/6.12.0/gcc_64']))
   command+=['-G','Ninja','-DCMAKE_BUILD_TYPE='+('RelWithDebInfo' if args.lane=='san' else 'Release'),'-DCMAKE_CXX_COMPILER='+('/usr/bin/clang++-24' if args.lane=='clang' else '/usr/bin/g++-16'),'-DCMAKE_PREFIX_PATH='+prefix,'-DLibtorrentRasterbar_DIR='+str(install/'lib/cmake/LibtorrentRasterbar'),'-DBIGLYBTC_LOCAL_ENGINE=ON','-DBIGLYBTC_DESKTOP=ON','-DBIGLYBTC_PERSISTENT_RUNTIME_MANIFEST=','-DBIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER=ON','-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL='+str(args.provider_approval),'-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256='+args.provider_approval_sha256,'-DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=OFF','-DBIGLYBTC_I2P_STREAMING_MANAGER_JAVA_ACCEPTANCE=OFF']
   if args.lane=='san':command+=['-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
   env['LD_LIBRARY_PATH']=str(install/'lib')+':/opt/Qt/6.12.0/gcc_64/lib'
  else:
   oldcache=base.cache_values(cache);assert oldcache['BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST']==str(a.BINDING) and pathlib.Path(oldcache['LibtorrentRasterbar_DIR']).resolve()==pathlib.Path(oldshipping['install_root'])/'lib/cmake/LibtorrentRasterbar'
   a.write(out/'cache-before.json',oldcache);command+=['-DBIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER=OFF','-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL=','-DBIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_SHA256=']
  runner.run('configure',command,timeout=300);cv=base.cache_values(cache);cachepin=a.sha(cache/'CMakeCache.txt');assert cv['CMAKE_GENERATOR']=='Ninja' and (cv['BIGLYBTC_PRIVATE_I2P_PROVIDER_OWNER']=='ON')==private
  if private:assert cv['BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST']=='' and pathlib.Path(cv['LibtorrentRasterbar_DIR']).resolve()==pathlib.Path(approved['install_root'])/'lib/cmake/LibtorrentRasterbar'
  tools=base.file_pins([cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/cmake','/usr/bin/ctest','/usr/bin/make',W/'run.py',W/'adopt.py',W/'jobs.mk']);versions={n:subprocess.check_output([n,'--version'],env=env,text=True).splitlines()[0] for n in [cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/make']}
  if cv.get('CMAKE_C_COMPILER'):tools.update(base.file_pins([cv['CMAKE_C_COMPILER']]));versions[cv['CMAKE_C_COMPILER']]=subprocess.check_output([cv['CMAKE_C_COMPILER'],'--version'],env=env,text=True).splitlines()[0]
  assert re.match(r'GNU Make 4\.4(?:\.|$)',versions['/usr/bin/make']) and re.match(r'1\.13(?:\.|$)',versions[cv['CMAKE_MAKE_PROGRAM']]);a.write(out/'tool-inputs.json',tools);a.write(out/'tool-versions.json',versions)
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',*targets,'--','-k','0'],timeout=7200);a.write(out/'build-receipt.json',dict(runner.rows[-1],source_manifest_sha256=a.sha(out/'native-source-map.json')))
  paths={t:cache/'tests'/t for t in suites.values()}
  if args.lane=='gcc':paths[PROBE]=cache/'tests'/PROBE
  if not private:paths['BiglyBTC']=cache/'app/BiglyBTC'
  images=s.images(paths);a.write(out/'images-before.json',images);deps=base.dependency_pins(paths,env,out);a.write(out/'dependencies-before.json',deps)
  wanted=approved['artifact']['sha256'] if private else oldshipping['artifact']['sha256']
  for row in a.load(out/'dependency-commands.json'):
   found=re.findall(r'libtorrent-rasterbar\.so\.2\.0 => (/\S+) ',row['stdout'])
   if found:assert len(found)==1 and a.sha(found[0])==wanted
  archive=cache/'services/libbiglybtc_i2p_provider_owner.a';archives={str(archive):a.sha(archive)} if private else {};a.write(out/'archives-before.json',archives)
  if private:generated=cache/'services/private_i2p_runtime_config.hpp';text=generated.read_text();approval=a.load(args.provider_approval);assert all(str(approval[k]) in text for k in ('artifact_sha256','artifact_bytes','source_capsule_sha256','sdk_tree_sha256','runtime_manifest_sha256','source_build_record_sha256','review_sha256'));a.write(out/'private-config.json',a.ref(generated))
  runner.run('inventory',['ctest','--test-dir',cache,'--show-only=json-v1'],timeout=60);inventory={x['name']:x for x in a.load(out/'inventory.log')['tests'] if x['name'] in suites};assert set(inventory)==set(suites)
  for name,target in suites.items():assert pathlib.Path(inventory[name]['command'][0]).resolve()==paths[target].resolve()
  with (W/'subject-phase.lock').open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX);phase={'scope':'affected ordinary owned tests only; no Public/Java/transfer','entered_monotonic_ns':time.monotonic_ns()}
   try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1800)
   finally:
    phase['left_monotonic_ns']=time.monotonic_ns();a.write(out/'subject-phase.json',phase);fcntl.flock(lock,fcntl.LOCK_UN);last=cache/'Testing/Temporary/LastTest.log'
    if last.exists():shutil.copyfile(last,out/'ctest-full.log')
  cases=list(ET.parse(out/'tests.xml').getroot().iter('testcase'));assert len(cases)==len(suites) and {x.get('name') for x in cases}==set(suites) and all(x.get('status')=='run' and all(x.find(t) is None for t in ('failure','error','skipped')) for x in cases)
  assert a.source_guard(args.source_commit,adoption)==before and a.protected()==protected and a.shipping()==oldshipping and provider(args)==approved and a.sha(N/'dist/BUILD_MANIFEST.json')==dist
  assert s.images(paths)==images and base.file_pins(deps)==deps and base.file_pins(tools)==tools and a.sha(cache/'CMakeCache.txt')==cachepin and all(a.sha(p)==pin for p,pin in archives.items())
  a.write(out/'source-after.json',a.source_guard(args.source_commit,adoption));a.write(out/'images-after.json',s.images(paths));a.write(out/'dependencies-after.json',base.file_pins(deps));a.write(out/'tools-after.json',base.file_pins(tools));a.write(out/'archives-after.json',archives)
  result.update(status=STATUS if private else DEFAULT_STATUS,actual_suites=len(suites),source_unchanged=True,images_unchanged=True,dependencies_unchanged=True,tools_unchanged=True,original_head_index_unchanged=protected,shipping_provider_unchanged=True,dist_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
 except BaseException as exc:result['failure']=repr(exc)
 finally:result['commands_sha256']=a.sha(out/'commands.json') if (out/'commands.json').exists() else None;result['artifacts_sha256']={str(p.relative_to(out)):a.sha(p) for p in out.rglob('*') if p.is_file() and p.parent==out};a.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'] in (STATUS,DEFAULT_STATUS) else 1
if __name__=='__main__':raise SystemExit(main())
