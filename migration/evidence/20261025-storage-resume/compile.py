from pathlib import Path
import hashlib,json,os,subprocess,sys,time
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
case=sys.argv[1];assert '/' not in case
b=n/os.environ.get('BIGLYBTC_STORAGE_BUILD','build/persistent-peer-accounting-20261021/bound-gcc')
jobs=int(os.environ.get('BIGLYBTC_STORAGE_JOBS','3'));assert 1<=jobs<=3
targets=sys.argv[2:] or ['biglybtc-original-file-io-tests','biglybtc-original-file-io-driver','biglybtc-managed-storage-tests','biglybtc-file-operations-tests','biglybtc-metadata-disk-router-tests','biglybtc-storage-handoff-driver','BiglyBTC']
source={str(p.relative_to(n)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((n/'preview/native').rglob('*')) if p.is_file()}
(w/(case+'-source.json')).write_text(json.dumps(source,indent=2)+'\n')
cmd=['nice','-n','5','cmake','--build',str(b),'--parallel',str(jobs),'--target',*targets,'--','-k','0'];start=time.monotonic()
with(w/(case+'.log')).open('w') as f:rc=subprocess.run(cmd,cwd=n,stdout=f,stderr=subprocess.STDOUT).returncode
(w/(case+'-receipt.json')).write_text(json.dumps({'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,2),'source_manifest_sha256':hashlib.sha256((w/(case+'-source.json')).read_bytes()).hexdigest()},indent=2)+'\n')
print(case,'exit',rc,'seconds',round(time.monotonic()-start,1))
if rc:
 lines=(w/(case+'.log')).read_text(errors='replace').splitlines();errors=[l for l in lines if 'error:' in l or 'undefined reference' in l or 'CMake Error' in l];print('\n'.join(errors[:18] or lines[-12:]))
raise SystemExit(rc)
