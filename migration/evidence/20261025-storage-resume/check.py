from pathlib import Path
import hashlib,json,os,re,subprocess,sys,time
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
case=sys.argv[1];suites=sys.argv[2:] or ['original-physical-file-io','original-storage-controls','original-disk-io','managed-storage','file-operations','metadata-disk-router']
assert '/' not in case
b=n/os.environ.get('BIGLYBTC_STORAGE_BUILD','build/persistent-peer-accounting-20261021/bound-gcc')
env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
source={str(p.relative_to(n)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((n/'preview/native').rglob('*')) if p.is_file()}
(w/(case+'-source.json')).write_text(json.dumps(source,indent=2)+'\n')
cmd=['nice','-n','5','ctest','--test-dir',str(b),'-R','^('+'|'.join(map(re.escape,suites))+')$','--no-tests=error','--output-on-failure','--verbose','--output-junit',str(w/(case+'.xml'))]
registered=json.loads(subprocess.check_output(['ctest','--test-dir',str(b),'--show-only=json-v1'],text=True));assert not set(suites)-{t['name'] for t in registered['tests']}
start=time.monotonic()
with(w/(case+'.log')).open('w') as f:rc=subprocess.run(cmd,cwd=n,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
(w/(case+'-receipt.json')).write_text(json.dumps({'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,2),'source_manifest_sha256':hashlib.sha256((w/(case+'-source.json')).read_bytes()).hexdigest()},indent=2)+'\n')
print(case,'exit',rc,'seconds',round(time.monotonic()-start,1))
if rc:
 lines=(w/(case+'.log')).read_text(errors='replace').splitlines();bad=[l for l in lines if 'FAIL' in l or 'fail' in l or 'exception' in l or 'Error' in l];print('\n'.join(bad[:22] or lines[-15:]))
raise SystemExit(rc)
