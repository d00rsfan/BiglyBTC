import hashlib,importlib.util,json,pathlib,subprocess,os
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent;T=R/'build/i2cp-isolated-router-20261029'
spec=importlib.util.spec_from_file_location('batch',H/'run-r3.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b);s=b.s
assert s.sha(T/'manifest.json')=='ea3835eddf67a2a92abe2085723ececd9647fa8a73e42d020505d598c0530cb7';m=s.load(T/'manifest.json')
print('manifest_keys',list(m),flush=True)
for name,pin in m.get('artifacts_sha256',m.get('artifacts',{})).items():assert s.sha(T/name)==pin
result=s.load(H/'runs/gcc-r3/result.json');source=result['source_commit'];assert result['status']=='PASS_PROFILE_IO_I2CP_TRACKER_COHORT';snapshot=s.source_snapshot(source)
values={}
for line in (s.CACHES['gcc']/'CMakeCache.txt').read_text().splitlines():
 if '=' in line and ':' in line and not line.startswith(('#','//')):
  name,value=line.split('=',1);values[name.split(':',1)[0]]=value
out=H/'router-build-r1';out.mkdir(mode=0o700,exist_ok=False);env=s.environment('gcc');runner=s.Runner(out,env)
cmake=['nice','-n','5','cmake','-S',T,'-B',out/'probe','-DCMAKE_CXX_COMPILER='+values['CMAKE_CXX_COMPILER'],'-DQt6_DIR='+values['Qt6_DIR'],'-DBIGLYBTC_NATIVE_SOURCE='+str(N/'preview/native'),'-DBIGLYBTC_NATIVE_BUILD='+str(s.CACHES['gcc'])]
for key in ('ZLIB_INCLUDE_DIR','ZLIB_LIBRARY_RELEASE','OPENSSL_INCLUDE_DIR','OPENSSL_CRYPTO_LIBRARY','OPENSSL_SSL_LIBRARY'):
 if key in values and 'NOTFOUND' not in values[key]:cmake.append('-D'+key+'='+values[key])
runner.run('configure',cmake,timeout=90);runner.run('build',['nice','-n','5','cmake','--build',out/'probe','--parallel','1'],timeout=180)
classes=out/'classes';classes.mkdir(mode=0o700)
jar=R/'build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar'
runner.run('javac',['nice','-n','5','/usr/bin/javac','-J-Xmx128m','-J-XX:ActiveProcessorCount=1','--release','17','-cp',jar,'-d',classes,T/'RouterTracker.java'],timeout=60)
assert s.source_snapshot(source)==snapshot
s.write(out/'result.json',{'status':'PASS_PROBE_BUILD_ONLY_NO_SUBJECTS','source_commit':source,'gcc_result_sha256':s.sha(H/'runs/gcc-r3/result.json'),'commands_sha256':s.sha(out/'commands.json'),'probe':str(out/'probe/biglybtc-i2cp-isolated-router-probe'),'class_count':len(list(classes.glob('*.class')))})
print('PROBE_AND_JAVA_COMPILED_NO_SUBJECTS',flush=True)
