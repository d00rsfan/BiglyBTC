"""Exact optional Public shared-quota admission;temporary generated outputs."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
B=Path(__file__).resolve().parents[1]/'engine/runtime-binding'
class SharedQuotaRuntimeInputs(unittest.TestCase):
 def invoke(self,root,manifest=None,library=None,include=None,optimized=False):
  source=B/'provider-public-shared-quota-runtime.json';m=json.loads(source.read_text())
  return subprocess.run([sys.executable,'-I','-B',*(['-O'] if optimized else []),str(B/'verify_runtime_manifest_public_shared_quota.py'),'--manifest',str(manifest or source),'--library',str(library or m['artifact']),'--include',str(include or m['include']),'--header',str(root/'header.hpp'),'--receipt',str(root/'receipt.json')],capture_output=True,timeout=30)
 def refused(self,root,result):
  self.assertNotEqual(result.returncode,0,result.stderr);self.assertFalse((root/'header.hpp').exists());self.assertFalse((root/'receipt.json').exists())
 def test_exact_sdk_and_public_only_receipt(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-shared-runtime-') as directory:
   root=Path(directory);result=self.invoke(root);self.assertEqual(result.returncode,0,result.stderr)
   receipt=json.loads((root/'receipt.json').read_text());self.assertEqual(receipt['exports'],59);self.assertEqual(receipt['roles'],['Public']);self.assertFalse(receipt['private_or_mixed_qualified']);self.assertIn('BIGLYBTC_PUBLIC_SHARED_QUOTA_BOUND 1',(root/'header.hpp').read_text())
 def test_parent_artifact_headers_and_manifest_refuse(self):
  current=json.loads((B/'provider-public-shared-quota-runtime.json').read_text());parent=json.loads(Path(current['parent_record']).read_text())
  for changes in [dict(library=parent['artifact']),dict(include=parent['include']),dict(manifest=current['parent_record'])]:
   with self.subTest(changes=changes),tempfile.TemporaryDirectory(prefix='biglybtc-shared-runtime-') as directory:self.refused(Path(directory),self.invoke(Path(directory),**changes))
 def test_manifest_changes_cannot_grant_roles_or_allocator_models(self):
  original=json.loads((B/'provider-public-shared-quota-runtime.json').read_text())
  for key,value in [('roles',['Public','Private']),('roles',['Public','Mixed']),('allocator_binding','TWO_SDK_IMAGES'),('class_abi','0x4242435300020001')]:
   with self.subTest(key=key,value=value),tempfile.TemporaryDirectory(prefix='biglybtc-shared-runtime-') as directory:
    root=Path(directory);m=dict(original);m[key]=value;path=root/'manifest.json';path.write_text(json.dumps(m));self.refused(root,self.invoke(root,manifest=path))
 def test_optimized_python_cannot_remove_sdk_checks(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-shared-runtime-') as directory:self.refused(Path(directory),self.invoke(Path(directory),optimized=True))
if __name__=='__main__':
 result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(SharedQuotaRuntimeInputs))
 if not result.wasSuccessful():raise SystemExit(1)
 print('PUBLIC_SHARED_QUOTA_RUNTIME_INPUTS four families PASS')
