#!/usr/bin/env python3
"""Launch one measured committed cohort; preserve file-only runner diagnostics."""
import argparse, hashlib, json, pathlib, re, subprocess, sys, time
T = pathlib.Path(__file__).resolve().parent
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p): return json.loads(pathlib.Path(p).read_bytes())
def main():
    p=argparse.ArgumentParser();p.add_argument('lane',choices=('private','san'));p.add_argument('--attempt',required=True);p.add_argument('--cohort',default='r5');a=p.parse_args()
    assert re.fullmatch(r'(?:gcc|san)-r[2-9][0-9]*',a.attempt) and re.fullmatch(r'r[1-9][0-9]*',a.cohort)
    cid=load(T/'source-milestone.json')['source_commit']
    receipt=T/'batch'/a.lane/a.cohort/'result-stamped.json';r=load(receipt)
    assert r['status']=='PASS' and r['source_commit']==cid
    probe=r['images']['biglybtc-mixed-network-host-transfer-probe']
    selection=load(T/'final-sdk-inputs.json')
    approval=pathlib.Path(selection['approval']);manifest=pathlib.Path(selection['runtime_manifest']);source=pathlib.Path(selection['source_capsule'])
    out=T/'acceptance/runs'/a.attempt;assert not out.exists()
    argv=[sys.executable,'-B',str(T/'acceptance/run.py'),'--source-commit',cid,
          '--build-receipt',str(receipt),'--probe',probe['path'],'--probe-sha256',probe['sha256'],
          '--provider-record',selection['source_build_record'],
          '--provider-source',str(source),'--provider-source-sha256',sha(source),
          '--provider-approval',str(approval),'--provider-approval-sha256',sha(approval),
          '--runtime-manifest',str(manifest),'--runtime-manifest-sha256',sha(manifest),
          '--output',str(out),'--acknowledge-warmed-local-cache-only']
    (T/'acceptance'/(a.attempt+'-launch.json')).write_text(json.dumps({'argv':argv,'source_commit':cid,'build_receipt_sha256':sha(receipt)},indent=2)+'\n')
    start=time.monotonic();log=T/'acceptance'/(a.attempt+'-launch.log')
    with log.open('xb') as f: code=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT).returncode
    result=load(out/'result.json') if (out/'result.json').is_file() else {'status':'PRELAUNCH_FAILURE'}
    row={'lane':a.lane,'exit_code':code,'status':result['status'],'elapsed_seconds':round(time.monotonic()-start,2),'result':str(out/'result.json'),'log':str(log)}
    (T/'acceptance'/(a.attempt+'-launch-result.json')).write_text(json.dumps(row,indent=2)+'\n');print(json.dumps(row),flush=True)
    return code
if __name__=='__main__':sys.exit(main())
