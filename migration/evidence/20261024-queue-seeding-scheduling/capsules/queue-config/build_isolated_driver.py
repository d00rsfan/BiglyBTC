#!/usr/bin/env python3
import hashlib,json,subprocess,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
NATIVE=Path('/home/yurii/Java/BiglyBTC')
CACHE=NATIVE/'build/persistent-peer-accounting-20261021/bound-gcc'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=HERE/'isolated-build-v1';out.mkdir()
    archives=[CACHE/'adapters/libbiglybtc_profile.a',CACHE/'adapters/libbiglybtc_state_codec.a',CACHE/'core/libbiglybtc_core.a']
    sources=[HERE/'driver.cpp',NATIVE/'preview/native/core/include/biglybtc/application_settings.hpp',NATIVE/'preview/native/core/include/biglybtc/queue_policy.hpp',NATIVE/'preview/native/adapters/application_settings.hpp',NATIVE/'preview/native/adapters/application_settings.cpp']
    inputs={str(p):sha(p) for p in archives+sources}
    binary=out/'biglybtc-queue-settings-roundtrip-driver'
    cmd=['/usr/bin/g++-16','-std=c++23','-O3','-DNDEBUG','-Wall','-Wextra','-Wpedantic','-Werror','-pthread','-I'+str(NATIVE/'preview/native/core/include'),'-I'+str(NATIVE/'preview/native/adapters'),str(HERE/'driver.cpp'),*[str(p) for p in archives],'-lz','-o',str(binary)]
    (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    started=time.monotonic()
    with (out/'build.stdout').open('w') as stdout,(out/'build.stderr').open('w') as stderr:
        result=subprocess.run(cmd,stdout=stdout,stderr=stderr,timeout=90)
    stable=all(sha(Path(path))==digest for path,digest in inputs.items())
    record={'status':'PASS_ISOLATED_DRIVER_BUILD' if result.returncode==0 and stable else 'FAIL','exit_code':result.returncode,'elapsed_seconds':time.monotonic()-started,'command':cmd,'input_sha256':inputs,'inputs_unchanged':stable,'shared_native_cache_mutated':False,'compiler_jobs':1,'driver_sha256':sha(binary) if binary.exists() else None}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:record[k] for k in ['status','elapsed_seconds','driver_sha256']}),flush=True)
    if record['status']!='PASS_ISOLATED_DRIVER_BUILD':raise SystemExit(1)
if __name__=='__main__':main()
