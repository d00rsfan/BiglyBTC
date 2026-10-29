"""One unexecuted maintenance suite after r3 stop-on-failure; no cohort pass credit."""
import importlib.util,pathlib,re,shutil
H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run-r3.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
source='f26966a0695dbe15126c3543c33b237d31789c8e';root=H/'runs/clang-r3';failed=s.load(root/'result.json');assert failed['status']=='FAIL' and failed['source_commit']==source
assert [x['exit'] for x in s.load(root/'commands.json')]==[0,0,0,8]
before=s.source_snapshot(source);assert s.load(root/'source-before.json')==before
suites={k:b.COMMON[k] for k in ('azureus-maintenance',)}
paths={n:s.CACHES['clang']/'tests'/n for n in suites.values()};images=s.images(paths)
assert images=={t:s.load(root/'images-before.json')[t] for t in paths}
assert 'azureus-maintenance' not in [t.attrib['name'] for t in __import__('xml.etree.ElementTree',fromlist=['ElementTree']).parse(root/'tests.xml').iter('testcase')]
out=H/'maintenance-supplement-r3';out.mkdir(exist_ok=False);env=s.environment('clang')
for k in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','CMAKE_BUILD_PARALLEL_LEVEL','BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT'):env.pop(k,None)
for k in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 d=out/k.lower();d.mkdir(mode=0o700);env[k]=str(d)
r=s.Runner(out,env);result={'status':'FAIL_SUPPLEMENTAL_MAINTENANCE_ONLY','source_commit':source,'images':images,'no_build':True,'no_pass_credit_to_failed_cohort':True,'runner_sha256':s.sha(__file__),'failed_result_sha256':s.sha(root/'result.json')}
try:
 r.run('new-maintenance-tests',['nice','-n','5','ctest','--test-dir',s.CACHES['clang'],'--parallel','1','-R','^('+'|'.join(map(re.escape,suites))+')$','--output-on-failure','--output-junit',out/'tests.xml'],timeout=260)
 b.base.check_xml(out/'tests.xml',suites);assert s.source_snapshot(source)==before and s.images(paths)==images
 result.update(status='PASS_SUPPLEMENTAL_MAINTENANCE_ONLY',actual_suites=1,tests_xml_sha256=s.sha(out/'tests.xml'))
except BaseException as exc:result['failure']=repr(exc)
finally:
 last=s.CACHES['clang']/'Testing/Temporary/LastTest.log'
 if last.exists():shutil.copyfile(last,out/'ctest-full.log')
 result['commands_sha256']=s.sha(out/'commands.json') if (out/'commands.json').exists() else None;s.write(out/'result.json',result)
print(result['status'],result.get('failure'),flush=True)
raise SystemExit(0 if result['status'].startswith('PASS_') else 1)
