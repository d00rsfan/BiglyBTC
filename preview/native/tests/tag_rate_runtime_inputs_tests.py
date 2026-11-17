"""Exact optional Public tag-rate SDK binding;generated inputs only."""
from pathlib import Path
import json,subprocess,sys,tempfile,unittest
B=Path(__file__).resolve().parents[1]/'engine/runtime-binding'
class TagRateRuntimeInputs(unittest.TestCase):
 def invoke(self,root,manifest=None,library=None,include=None):
  source=B/'provider-public-tag-rates-runtime.json';m=json.loads(source.read_text())
  return subprocess.run([sys.executable,'-I','-B',str(B/'verify_runtime_manifest_public_tag_rates.py'),'--manifest',str(manifest or source),'--library',library or m['artifact'],'--include',include or m['include'],'--header',str(root/'header.hpp'),'--receipt',str(root/'receipt.json')],capture_output=True,timeout=30)
 def refused(self,root,result):
  self.assertNotEqual(result.returncode,0,result.stderr);self.assertFalse((root/'header.hpp').exists());self.assertFalse((root/'receipt.json').exists())
 def test_exact_source_headers_and_artifact(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-tag-runtime-') as directory:
   root=Path(directory);result=self.invoke(root);self.assertEqual(result.returncode,0,result.stderr)
   receipt=json.loads((root/'receipt.json').read_text());self.assertEqual(receipt['exports'],51);self.assertEqual(receipt['roles'],['Public']);self.assertIn('BIGLYBTC_PUBLIC_TAG_RATES_BOUND 1',(root/'header.hpp').read_text())
 def test_old_artifact_and_headers_refuse(self):
  m=json.loads((B/'provider-public-tag-rates-runtime.json').read_text());old=json.loads(Path(m['parent_record']).read_text())
  for change in [dict(library=old['artifact']),dict(include=old['include'])]:
   with self.subTest(change=tuple(change)),tempfile.TemporaryDirectory(prefix='biglybtc-tag-runtime-') as directory:
    root=Path(directory);self.refused(root,self.invoke(root,**change))
 def test_manifest_cannot_grant_other_roles(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-tag-runtime-') as directory:
   root=Path(directory);m=json.loads((B/'provider-public-tag-rates-runtime.json').read_text());m['roles']=['Public','I2P'];path=root/'manifest.json';path.write_text(json.dumps(m));self.refused(root,self.invoke(root,manifest=path))
 def test_existing_chat_manifest_is_not_a_directional_grant(self):
  m=json.loads((B/'provider-public-tag-rates-runtime.json').read_text())
  with tempfile.TemporaryDirectory(prefix='biglybtc-tag-runtime-') as directory:
   root=Path(directory);self.refused(root,self.invoke(root,manifest=Path(m['parent_record'])))
if __name__=='__main__':
 result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(TagRateRuntimeInputs))
 if not result.wasSuccessful():raise SystemExit(1)
 print('TAG_RATE_RUNTIME_INPUTS four families PASS')
