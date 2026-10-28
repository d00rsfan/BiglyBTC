"""Exact PORT source/SDK and stager refusal controls; no builds/profile/socket IO."""
from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
B=Path(__file__).resolve().parents[1]/'engine/runtime-binding'
MANIFEST='provider-original-identity-runtime.json'
class OriginalIdentityInputs(unittest.TestCase):
 def run_case(self,root,manifest,library=None,headers=None):
  value=json.loads((B/MANIFEST).read_bytes())
  return subprocess.run([sys.executable,'-B',str(B/'verify_runtime_manifest_original_identity.py'),'--manifest',str(manifest),'--library',library or value['artifact'],'--include',headers or value['install_root']+'/include','--header',str(root/'generated.hpp'),'--receipt',str(root/'generated.json')],capture_output=True,timeout=45)
 def rejected(self,root,result):
  self.assertNotEqual(result.returncode,0,result.stdout)
  self.assertFalse((root/'generated.hpp').exists());self.assertFalse((root/'generated.json').exists())
 def test_exact_source_and_installed_sdk(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-mainline-input-') as name:
   root=Path(name);result=self.run_case(root,B/MANIFEST);self.assertEqual(result.returncode,0,result.stderr)
   self.assertTrue((root/'generated.hpp').is_file());receipt=json.loads((root/'generated.json').read_bytes())
   self.assertIs(receipt['provider_original_identity'],True);self.assertIs(receipt['provider_tracker_response'],True);self.assertIs(receipt['causal_qualified'],False)
 def test_wrong_parent_artifact_and_sdk_emit_nothing(self):
  old=json.loads((B/'provider-socks-runtime.json').read_bytes())
  for change in [dict(library=old['artifact']),dict(headers=old['install_root']+'/include')]:
   with self.subTest(change=change),tempfile.TemporaryDirectory(prefix='biglybtc-mainline-input-') as name:
    root=Path(name);self.rejected(root,self.run_case(root,B/MANIFEST,**change))
 def test_manifest_cannot_self_approve(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-mainline-input-') as name:
   root=Path(name);value=json.loads((B/MANIFEST).read_bytes());value['causal_qualified']=True;manifest=root/MANIFEST;manifest.write_text(json.dumps(value))
   self.rejected(root,self.run_case(root,manifest))
 def test_changed_record_emits_nothing(self):
  with tempfile.TemporaryDirectory(prefix='biglybtc-mainline-input-') as name:
   root=Path(name);manifest=root/MANIFEST;manifest.write_bytes((B/MANIFEST).read_bytes());(root/'provider-original-identity-source-build-record.json').write_text('{}\n')
   self.rejected(root,self.run_case(root,manifest))
 def test_stager_requires_actual_compiled_receipt_and_header(self):
  stager=Path(__file__).resolve().parents[2]/'migration/tools/stage_preview.py';sys.dont_write_bytecode=True
  spec=importlib.util.spec_from_file_location('mainline_owned_stager_control',stager);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  value=json.loads((B/MANIFEST).read_bytes())
  for changed in ('receipt','tracker-receipt','header'):
   with self.subTest(changed=changed),tempfile.TemporaryDirectory(prefix='biglybtc-mainline-stager-') as name:
    root=Path(name);result=self.run_case(root,B/MANIFEST);self.assertEqual(result.returncode,0,result.stderr)
    generated=root/'build/engine/runtime-binding';generated.mkdir(parents=True)
    (generated/'persistent_runtime_config.hpp').write_bytes((root/'generated.hpp').read_bytes())
    receipt=json.loads((root/'generated.json').read_bytes())
    if changed=='receipt':receipt['provider_original_identity']=False
    if changed=='tracker-receipt':receipt['provider_tracker_response']=False
    (generated/'configure-receipt.json').write_text(json.dumps(receipt))
    if changed=='header':
     with (generated/'persistent_runtime_config.hpp').open('a') as out:out.write('// changed compiled binding\n')
    with self.assertRaisesRegex(ValueError,'Application (was not configured|compiled binding)'):
     module.private_original_identity_source_libtorrent(Path(value['artifact']),B/MANIFEST,root/'build')
 def test_private_selectors_cannot_be_combined_before_output(self):
  stager=Path(__file__).resolve().parents[2]/'migration/tools/stage_preview.py'
  with tempfile.TemporaryDirectory(prefix='biglybtc-mainline-selector-') as name:
   root=Path(name);output=root/'never-created';value=json.loads((B/MANIFEST).read_bytes())
   result=subprocess.run([sys.executable,'-B',str(stager),'--build',str(root/'no-build'),'--output',str(output),'--libtorrent-package',str(root/'no-package'),'--source-libtorrent',value['artifact'],'--private-original-identity-provider-manifest',str(B/MANIFEST),'--private-socks-provider-manifest',str(B/'provider-socks-runtime.json')],capture_output=True,timeout=15)
   self.assertNotEqual(result.returncode,0);self.assertIn(b'Select one explicit private or existing provider manifest',result.stderr);self.assertFalse(output.exists())
if __name__=='__main__':unittest.main()
