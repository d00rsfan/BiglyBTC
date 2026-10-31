"""Closed mixed role/source inputs: real approval positive, altered/old role negative."""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser()
 for name in ('manifest','provider','include-root','approval'):p.add_argument('--'+name,required=True,type=Path)
 p.add_argument('--approval-sha256',required=True);a=p.parse_args()
 verifier=Path(__file__).resolve().parents[1]/'engine/runtime-binding/verify_runtime_manifest_mixed.py'
 original={q.resolve():digest(q) for q in (a.manifest,a.provider,a.approval)}
 cases=['exact-approved','proposal','wrong-digest','wrong-artifact','private-only-schema','old-coexist-schema','missing-mixed-role','actual-old-private','actual-old-coexistence','self-approved-manifest','old-public-sdk']
 for case in cases:
  with tempfile.TemporaryDirectory(prefix='biglybtc-mixed-input-') as temporary:
   root=Path(temporary);approval=a.approval;pin=a.approval_sha256;manifest=a.manifest;include=a.include_root
   if case in ('proposal','wrong-artifact','private-only-schema','old-coexist-schema','missing-mixed-role'):
    v=json.loads(approval.read_bytes())
    if case=='proposal':v['status']='PROPOSAL_NOT_APPROVED'
    elif case=='wrong-artifact':v['artifact_sha256']='0'*64
    elif case=='private-only-schema':v['schema']='BIGLYBTC_PRIVATE_I2P_OWNER_APPROVAL_V1';v['source_capsule_sha256']='63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3'
    elif case=='old-coexist-schema':v['schema']='BIGLYBTC_PUBLIC_PRIVATE_COEXISTENCE_APPROVAL_V1';v['status']='ROOT_APPROVED_PUBLIC_PRIVATE_COEXISTENCE'
    else:v['roles'].remove('MixedI2pTransportV1')
    approval=root/'approval.json';approval.write_text(json.dumps(v)+'\n');pin=digest(approval)
   elif case in ('actual-old-private','actual-old-coexistence'):
    key='private' if case=='actual-old-private' else 'coexistence';old=json.loads(manifest.read_bytes())['rejected_prior_approvals'][key];approval=Path(old['path']);pin=old['sha256'];assert digest(approval)==pin
   elif case=='wrong-digest':pin='0'*64
   elif case=='self-approved-manifest':
    v=json.loads(manifest.read_bytes());v['native_runtime_approved']=True;manifest=root/'manifest.json';manifest.write_text(json.dumps(v)+'\n')
   elif case=='old-public-sdk':
    public=json.loads((verifier.parent/'provider-mainline-routing-runtime.json').read_bytes());include=Path(public['install_root'])/'include'
   command=[sys.executable,'-B',str(verifier),'--manifest',str(manifest),'--library',str(a.provider),'--include',str(include),'--approval',str(approval),'--approval-sha256',pin,'--header',str(root/'generated.hpp'),'--receipt',str(root/'generated.json')]
   actual=subprocess.run(command,capture_output=True,timeout=60)
   if case=='exact-approved':
    assert actual.returncode==0,actual.stderr.decode(errors='replace');r=json.loads((root/'generated.json').read_bytes());assert r['same_image_exports']==39 and r['native_mixed_qualification_pending'] is True
    assert '#define BIGLYBTC_PERSISTENT_MIXED_I2P_BOUND 1' in (root/'generated.hpp').read_text()
   else:assert actual.returncode!=0 and not(root/'generated.hpp').exists() and not(root/'generated.json').exists(),case
   print('PASS mixed-input '+case)
 assert all(digest(q)==h for q,h in original.items())
if __name__=='__main__':main()
