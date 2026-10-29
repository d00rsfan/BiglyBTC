"""Unexecuted new Enc4 cases only; no pass credit to failed compile cohort."""
import importlib.util,pathlib,re,shutil
H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
source='2fe669f688b4e0b64ba7205a64c4e969ca828281';root=H/'runs/clang-r1';failed=s.load(root/'result.json');assert failed['status']=='FAIL' and failed['source_commit']==source
assert [x['exit'] for x in s.load(root/'commands.json')]==[0,1]
before=s.source_snapshot(source);assert s.load(root/'source-before.json')==before
suites={k:b.COMMON[k] for k in ('pure-i2cp-leaseset2','i2cp-initial-channel')};build=(root/'build.log').read_text()
for target in suites.values():assert re.search(r'^\[\d+/\d+\] Linking CXX executable tests/'+re.escape(target)+r'$',build,re.M),target
paths={n:s.CACHES['clang']/'tests'/n for n in suites.values()};images=s.images(paths)
out=H/'enc4-supplement-r1';out.mkdir(exist_ok=False);env=s.environment('clang')
for k in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT'):env.pop(k,None)
for k in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 d=out/k.lower();d.mkdir(mode=0o700);env[k]=str(d)
r=s.Runner(out,env);result={'status':'FAIL_SUPPLEMENTAL_NEW_ENC4_ONLY','source_commit':source,'images':images,'no_build':True,'no_pass_credit_to_failed_cohort':True,'runner_sha256':s.sha(__file__),'failed_result_sha256':s.sha(root/'result.json')}
try:
 r.run('new-enc4-tests',['nice','-n','5','ctest','--test-dir',s.CACHES['clang'],'--parallel','1','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=180)
 b.base.check_xml(out/'tests.xml',suites);assert s.source_snapshot(source)==before and s.images(paths)==images
 result.update(status='PASS_SUPPLEMENTAL_NEW_ENC4_ONLY',actual_suites=2,tests_xml_sha256=s.sha(out/'tests.xml'))
except BaseException as exc:result['failure']=repr(exc)
finally:
 last=s.CACHES['clang']/'Testing/Temporary/LastTest.log'
 if last.exists():shutil.copyfile(last,out/'ctest-full.log')
 result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
print(result['status'],result.get('failure'),flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
