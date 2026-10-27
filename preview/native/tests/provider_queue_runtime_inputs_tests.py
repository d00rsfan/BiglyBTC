"""Actual exact queue provider configure admission and negative SDK/artifact controls."""
from pathlib import Path
import json,subprocess,sys,tempfile,unittest
B=Path(__file__).resolve().parents[1]/'engine/runtime-binding'
class QueueInputs(unittest.TestCase):
 def run_case(self,root,manifest,library=None,headers=None):
  value=json.loads((B/'provider-queue-runtime.json').read_bytes());return subprocess.run([sys.executable,'-B',str(B/'verify_runtime_manifest_queue.py'),'--manifest',str(manifest),'--library',library or value['artifact'],'--include',headers or value['install_root']+'/include','--header',str(root/'generated.hpp'),'--receipt',str(root/'generated.json')],capture_output=True,timeout=45)
 def test_exact_source_and_installed_sdk(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-queue-input-') as name:
   root=Path(name);r=self.run_case(root,B/'provider-queue-runtime.json');self.assertEqual(r.returncode,0,r.stderr);self.assertTrue((root/'generated.hpp').is_file())
 def test_wrong_artifact_and_sdk_emit_nothing(self):
  old=json.loads((B/'provider-webseed-runtime.json').read_bytes())
  for change in [dict(library=old['artifact']),dict(headers=old['install_root']+'/include')]:
   with self.subTest(change=change),tempfile.TemporaryDirectory(prefix='biglybtc-queue-input-') as name:
    root=Path(name);r=self.run_case(root,B/'provider-queue-runtime.json',**change);self.assertNotEqual(r.returncode,0);self.assertFalse((root/'generated.hpp').exists());self.assertFalse((root/'generated.json').exists())
 def test_manifest_cannot_self_approve(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-queue-input-') as name:
   root=Path(name);m=json.loads((B/'provider-queue-runtime.json').read_bytes());m['causal_qualified']=True;p=root/'manifest.json';p.write_text(json.dumps(m));r=self.run_case(root,p);self.assertNotEqual(r.returncode,0);self.assertFalse((root/'generated.hpp').exists());self.assertFalse((root/'generated.json').exists())
 def test_changed_record_emits_nothing(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-queue-input-') as name:
   root=Path(name);p=root/'provider-queue-runtime.json';p.write_bytes((B/p.name).read_bytes());(root/'provider-queue-source-build-record.json').write_text('{}\n');r=self.run_case(root,p);self.assertNotEqual(r.returncode,0);self.assertFalse((root/'generated.hpp').exists());self.assertFalse((root/'generated.json').exists())
if __name__=='__main__':unittest.main()
