import importlib.util,pathlib,shutil
R=pathlib.Path('/home/yurii/Java/BiglyBT');H=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('batch',H/'run.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
source=s.load(H/'root-adoption.json')['source_commit'];original=H/'runs/clang-r1';failure=s.load(original/'result.json');assert failure['failure']=="AssertionError('tests')" and s.load(original/'commands.json')[1]['exit']==0
before=s.source_snapshot(source);images=s.load(original/'images-before.json');assert all(s.sha(x['path'])==x['sha256'] for x in images.values())
out=H/'actor-supplement-r1';out.mkdir(parents=True,exist_ok=False);env=s.environment('clang')
for k in ('ASAN_OPTIONS','LSAN_OPTIONS','UBSAN_OPTIONS','LD_DEBUG_OUTPUT','BIGLYBTC_PREVIEW_PROFILE','BIGLYBTC_HTTP_PRIVATE_RUNTIME','CMAKE_BUILD_PARALLEL_LEVEL'):env.pop(k,None)
for k in ('HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'):
 p=out/k.lower();p.mkdir(mode=0o700);env[k]=str(p)
env['BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT']=str(out/'actual-java-peer');runner=s.Runner(out,env);result={'status':'FAIL','source_commit':source,'no_build':True,'same_compiled_images_sha256':s.sha(original/'images-before.json'),'no_pass_credit_to_failed_cohort':True}
try:
 runner.run('actor-tests',['nice','-n','5','ctest','--test-dir',s.CACHES['clang'],'--parallel','1','-R','^i2p-original-streaming-peer$','--output-on-failure','--output-junit',out/'actor-tests.xml'],timeout=540)
 b.check_xml(out/'actor-tests.xml',['i2p-original-streaming-peer']);result['status']='PASS_SUPPLEMENTAL_ACTOR_ONLY'
except BaseException as exc:result['failure']=repr(exc)
finally:
 shutil.copyfile(s.CACHES['clang']/'Testing/Temporary/LastTest.log',out/'ctest-full.log');assert s.source_snapshot(source)==before and all(s.sha(x['path'])==x['sha256'] for x in images.values());result['commands_sha256']=s.sha(out/'commands.json');s.write(out/'result.json',result)
print(result['status'],flush=True)
