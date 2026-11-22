"""Exercise exact successor closure and preserve the legacy role lane."""
from pathlib import Path
import json,subprocess,sys,tempfile,unittest
B=Path(__file__).resolve().parents[1]/'engine/runtime-binding'
class PolicyInputs(unittest.TestCase):
 def invoke(self,root,legacy=False,optimized=False,**changes):
  stem='provider-shared-quota-all-roles' if legacy else 'provider-original-rate-policy'
  m=B/(stem+'-runtime.json');a=B/(stem+'-approval.json');values=json.loads(m.read_text())
  import hashlib
  args=dict(manifest=m,approval=a,approval_sha256=hashlib.sha256(a.read_bytes()).hexdigest(),library=values['artifact'],include=values['include'],header=root/'out.hpp',receipt=root/'out.json');args.update(changes)
  argv=[sys.executable,'-I','-B',*(['-O'] if optimized else []),str(B/'verify_runtime_manifest_shared_roles.py')]
  for key,value in args.items():argv+=['--'+key.replace('_','-'),str(value)]
  return subprocess.run(argv,capture_output=True,timeout=90)
 def test_both_exact_qualifications_remain_distinct(self):
  for legacy in [True,False]:
   with self.subTest(legacy=legacy),tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);r=self.invoke(p,legacy=legacy);self.assertEqual(r.returncode,0,r.stderr)
    data=json.loads((p/'out.json').read_text());self.assertEqual(data['exports'],59 if legacy else 61);self.assertFalse(data['private_tag_payload_qualified'])
 def test_old_artifact_headers_and_approval_do_not_grant_new_policy(self):
  old=json.loads((B/'provider-shared-quota-all-roles-runtime.json').read_text())
  for change in [dict(library=old['artifact']),dict(include=old['include']),dict(approval=B/'provider-shared-quota-all-roles-approval.json'),dict(approval_sha256='0'*64)]:
   with self.subTest(change=change),tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);r=self.invoke(p,**change);self.assertNotEqual(r.returncode,0);self.assertFalse((p/'out.hpp').exists());self.assertFalse((p/'out.json').exists())
 def test_manifest_role_edits_and_optimized_python_refuse(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);m=json.loads((B/'provider-original-rate-policy-runtime.json').read_text());m['private_tag_payload_qualified']=True;f=p/'changed.json';f.write_text(json.dumps(m))
   for args in [dict(manifest=f),dict(optimized=True)]:
    r=self.invoke(p,**args);self.assertNotEqual(r.returncode,0);self.assertFalse((p/'out.hpp').exists())
if __name__=='__main__':unittest.main()
