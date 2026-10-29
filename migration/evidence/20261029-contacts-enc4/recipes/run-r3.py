#!/usr/bin/env python3
"""Root-only combined maintenance/contacts + Enc4 qualification; import is inert."""
import argparse,importlib.util,os,pathlib,re,shutil,stat
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
BASE=R/'build/routing-streaming-followup-20261029/run-r3.py'
spec=importlib.util.spec_from_file_location('base',BASE);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base);s=base.s
COMMON={**{k:v for k,v in base.COMMON.items() if k!='pure-i2cp-datagram-messages'},'pure-i2cp-leaseset2':'biglybtc-i2cp-leaseset-tests','azureus-maintenance':'biglybtc-azureus-maintenance-tests','azureus-database-runtime':'biglybtc-azureus-database-runtime-tests'}
PRODUCTS=base.PRODUCTS;PROVIDER_SHA=base.PROVIDER_SHA;provider_pins=base.provider_pins
SUITES={**COMMON,**base.GCC_UI}
def suites_for_lane(lane):return SUITES if lane=='gcc' else COMMON
def main():
 p=argparse.ArgumentParser();p.add_argument('lane',choices=['gcc','clang','san']);p.add_argument('--attempt',required=True);a=p.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',a.attempt)
 flags=os.environ.get('MAKEFLAGS','');fifo=re.search(r'--jobserver-auth=fifo:([^\s]+)',flags)
 assert fifo and re.search(r'(?:^|\s)-j3(?:\s|$)',flags) and stat.S_ISFIFO(pathlib.Path(fifo[1]).stat().st_mode)
 adoption_path=H/'root-adoption-r3.json';adoption=s.load(adoption_path);source=adoption['source_commit'];snapshot=s.source_snapshot(source)
 cache=s.CACHES[a.lane];out=H/'runs'/(a.lane+'-'+a.attempt);out.mkdir(parents=True,exist_ok=False);env=s.environment(a.lane)
 for k in ('CMAKE_BUILD_PARALLEL_LEVEL','ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','QT_TLS_BACKEND'):env.pop(k,None)
 if a.lane=='san':env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 assert env['MAKEFLAGS']==flags
 for k in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
  d=out/k.lower();d.mkdir(mode=0o700);env[k]=str(d)
 runner=s.Runner(out,env);suites=suites_for_lane(a.lane);products=PRODUCTS if a.lane=='gcc' else {};targets=[*suites.values(),*products]
 result={'status':'FAIL','source_commit':source,'lane':a.lane,'cache':str(cache),'compiled_targets':targets,'provider_rebuilt':False,'historical_passes_pooled':False,'runner_sha256':s.sha(__file__),'source_receipt_sha256':s.sha(adoption_path),'source_path_count':len(adoption['source_paths']),'jobserver_makeflags':flags}
 try:
  s.write(out/'source-before.json',snapshot);s.write(out/'native-source-map.json',snapshot['files_sha256'])
  for name,row in adoption['source_paths'].items():assert s.sha(N/name)==row['after_sha256'],name
  provider=provider_pins();result['provider']=provider;distpin=s.sha(N/'dist/BUILD_MANIFEST.json');result['dist_manifest_before']=distpin
  cv=base.cache_values(cache);assert cv['CMAKE_GENERATOR']=='Ninja'
  tools=base.file_pins([cv['CMAKE_CXX_COMPILER'],cv['CMAKE_MAKE_PROGRAM'],'/usr/bin/make','/usr/bin/cmake','/usr/bin/ctest',base.SUPPORT,BASE,pathlib.Path(__file__),H/'jobs-r3.mk']);s.write(out/'tool-inputs.json',{'files':tools,'jobserver_fifo':fifo[1]})
  runner.run('configure',['nice','-n','5','cmake','-S',N/'preview/native','-B',cache],timeout=300);cachepin=s.sha(cache/'CMakeCache.txt')
  runner.run('build',['nice','-n','5','cmake','--build',cache,'--target',*targets,'--','-k','0'],timeout=3600)
  s.write(out/'build-receipt.json',dict(runner.rows[-1],source_manifest_sha256=s.sha(out/'native-source-map.json')))
  paths={**{n:cache/'tests'/n for n in suites.values()},**{n:cache/p for n,p in products.items()}};images=s.images(paths);s.write(out/'images-before.json',images)
  deps=base.dependency_pins(paths,env,out);s.write(out/'dependencies-before.json',deps)
  runner.run('inventory',['ctest','--test-dir',cache,'--show-only=json-v1'],timeout=60);inventory=s.load(out/'inventory.log');selected={t['name']:t for t in inventory['tests'] if t['name'] in suites};assert set(selected)==set(suites)
  for name,target in suites.items():assert pathlib.Path(selected[name]['command'][0]).resolve()==paths[target].resolve(),name
  try:runner.run('tests',['nice','-n','5','ctest','--test-dir',cache,'--parallel','1','--stop-on-failure','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=1800)
  finally:
   last=cache/'Testing/Temporary/LastTest.log'
   if last.exists():shutil.copyfile(last,out/'ctest-full.log')
  actual=base.check_xml(out/'tests.xml',suites);assert not re.search(r'^(?:SKIP\s*:|FAIL!)',(out/'ctest-full.log').read_text(errors='replace'),re.M)
  assert s.source_snapshot(source)==snapshot and s.images(paths)==images and provider_pins()==provider and base.file_pins(deps)==deps and base.file_pins(tools)==tools
  assert s.sha(cache/'CMakeCache.txt')==cachepin and s.sha(N/'dist/BUILD_MANIFEST.json')==distpin
  s.write(out/'source-after.json',s.source_snapshot(source));s.write(out/'images-after.json',s.images(paths))
  result.update(status='PASS_ENC4_LEASESET_COHORT',suite_names=actual,actual_suites=len(actual),tests_xml_sha256=s.sha(out/'tests.xml'),full_ctest_log_sha256=s.sha(out/'ctest-full.log'),source_unchanged=True,images_unchanged=True,sanitizer_environment={k:env[k] for k in ('ASAN_OPTIONS','UBSAN_OPTIONS') if k in env})
 except BaseException as exc:result['failure']=repr(exc)
 finally:
  result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
 print(result['status'],out/'result.json',flush=True);return 0 if result['status'].startswith('PASS_') else 1
if __name__=='__main__':raise SystemExit(main())
