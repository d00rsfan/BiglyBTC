#!/usr/bin/env python3
"""One native-only replay against retained actual Java; never launch Java."""
import hashlib,importlib.util,json,os,subprocess,sys,time
from pathlib import Path
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
BASE=HERE/'signature-amendment'
DRIVER=Path('/home/yurii/Java/BiglyBTC/build/persistent-peer-accounting-20261021/bound-gcc/tests/biglybtc-queue-rank-oracle-driver')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=HERE/'final-native-replay-v1';out.mkdir()
    old=json.loads((BASE/'run-r1/result.json').read_text())
    manifest=json.loads((BASE/'manifest.json').read_text())
    assert old['status']=='PASS' and old['admitted_scalar_comparisons']==190
    assert sha(BASE/'run-r1/original.stdout')==old['original_stdout_sha256']
    assert sha(BASE/'vectors.json')==old['vectors_sha256']
    for name in ['vectors.json','vectors.tsv','compare.py']:
        assert sha(BASE/name)==manifest['artifact_sha256'][name]
    before=sha(DRIVER);start=time.monotonic()
    env={k:v for k,v in os.environ.items() if k!='LD_PRELOAD'}
    with (BASE/'vectors.tsv').open('rb') as source,(out/'native.stdout').open('w') as stdout,(out/'native.stderr').open('w') as stderr:
        result=subprocess.run([str(DRIVER)],stdin=source,stdout=stdout,stderr=stderr,env=env,timeout=45)
    assert result.returncode==0 and sha(DRIVER)==before
    module=importlib.util.spec_from_file_location('retained_queue_compare',BASE/'compare.py')
    compare=importlib.util.module_from_spec(module);module.loader.exec_module(compare)
    receipt=compare.compare(BASE/'vectors.json',BASE/'run-r1/original.stdout',out/'native.stdout')
    receipt.update({'native_driver_sha256':before,'native_only_replay':True,'java_reexecuted':False,'original_receipt_sha256':sha(BASE/'run-r1/result.json'),'original_receipt':str(BASE/'run-r1/result.json'),'command':[str(DRIVER)],'stdin':str(BASE/'vectors.tsv'),'stdin_sha256':sha(BASE/'vectors.tsv'),'exit_code':result.returncode,'elapsed_seconds':time.monotonic()-start})
    (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:receipt[k] for k in ['status','admitted_scalar_comparisons','native_driver_sha256','java_reexecuted']}),flush=True)
    if receipt['status']!='PASS':raise SystemExit(1)
if __name__=='__main__':main()
