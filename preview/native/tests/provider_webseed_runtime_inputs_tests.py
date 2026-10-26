"""Exact source/SDK webseed binding inputs. Root executes after source adoption."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode=True
BINDING=Path(__file__).resolve().parents[1]/"engine/runtime-binding"
MANIFEST_SHA="ea82f78982a1428eb64067719cf30bbca7cfd09dd2abfcf8f76024f6e6ed41d5"
RECORD_SHA="a2692224abe745899c52cee6f1535d7a9db0d995e1e863ec50f2b9323f4bbf08"


class WebseedInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location("webseed_exact_outer",BINDING/"verify_runtime_manifest_webseed.py")
        cls.verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.verifier)
        cls.path=BINDING/"provider-webseed-runtime.json"
        cls.manifest=json.loads(cls.path.read_bytes())
        cls.artifact=Path(cls.manifest["artifact"]);cls.include=Path(cls.manifest["install_root"])/"include"

    def clone(self,root):
        for name in ("provider-webseed-runtime.json","provider-webseed-source-build-record.json",
                     "provider-next-source-build-record.json"):
            (root/name).write_bytes((BINDING/name).read_bytes())
        return root/"provider-webseed-runtime.json"

    def refused(self,path,root,library=None,headers=None):
        header,receipt=root/"refused.hpp",root/"refused.json"
        result=subprocess.run([sys.executable,"-B",str(BINDING/"verify_runtime_manifest_webseed.py"),
            "--manifest",str(path),"--library",str(library or self.artifact),
            "--include",str(headers or self.include),"--header",str(header),"--receipt",str(receipt)],
            env=dict(os.environ,PYTHONDONTWRITEBYTECODE="1"),capture_output=True,timeout=45)
        self.assertNotEqual(result.returncode,0,result.stdout)
        self.assertFalse(header.exists());self.assertFalse(receipt.exists())
        return result.stderr

    def test_exact_webseed_and_old_provider_branches_remain_scoped(self):
        actual,pin=self.verifier.inspect(self.path,self.artifact,self.include)
        self.assertEqual(pin,MANIFEST_SHA);self.assertFalse(actual["causal_qualified"])
        self.assertEqual(self.verifier.RECORD_SHA,RECORD_SHA)
        for name,expected in (("provider-next-runtime.json","3342ec8ae72ecafa4a1b887e664d0d066fffce4f7d864b0b130ca903be953a3a"),
                              ("provider-runtime.json","e74def09b2257a266786b8ddb116382d300716687bf26bbe57072f5ff90e8737")):
            old=json.loads((BINDING/name).read_bytes())
            result,pin=self.verifier.inspect(BINDING/name,Path(old["artifact"]),Path(old["install_root"])/"include")
            self.assertEqual(pin,expected);self.assertFalse(result["causal_qualified"])

    def test_manifest_cannot_self_approve_or_claim_causal_qualification(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-webseed-input-") as area:
            root=Path(area);path=self.clone(root)
            path.write_text(json.dumps(dict(self.manifest,causal_qualified=True)))
            self.assertIn(b"reviewed catalog",self.refused(path,root))

    def test_changed_and_missing_record_have_no_generated_outputs(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-webseed-input-") as area:
            root=Path(area);path=self.clone(root);record=root/"provider-webseed-source-build-record.json"
            record.write_bytes(b"{}\n")
            self.assertIn(b"Changed webseed",self.refused(path,root));record.unlink()
            self.refused(path,root)

    def test_changed_parent_record_is_not_inherited_authority(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-webseed-input-") as area:
            root=Path(area);path=self.clone(root)
            (root/"provider-next-source-build-record.json").write_bytes(b"{}\n")
            self.assertIn(b"Changed webseed",self.refused(path,root))

    def test_imported_sdk_and_unbound_same_bytes_copy_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="biglybtc-webseed-input-") as area:
            root=Path(area);include=root/"include";include.mkdir()
            self.assertIn(b"Imported LT library/SDK differs",self.refused(self.path,root,headers=include))
            image=root/self.artifact.name;image.write_bytes(self.artifact.read_bytes())
            self.assertIn(b"Imported LT library/SDK differs",self.refused(self.path,root,library=image))

    def test_whole_source_map_cannot_hide_an_extra_or_rewritten_leaf(self):
        # Controlled record primitive; no mutation of any qualified source/tree.
        helpers=self.verifier.parent_helpers();common=helpers["verify_runtime_manifest.py"]
        record=json.loads((BINDING/"provider-webseed-source-build-record.json").read_bytes())
        parent=json.loads((BINDING/"provider-next-source-build-record.json").read_bytes())
        for key,value in (("unrecorded.cpp","0"*64),("src/torrent.cpp","0"*64)):
            changed=copy.deepcopy(record);changed["source_inputs_sha256"][key]=value
            with self.assertRaisesRegex(ValueError,"exact parent plus three"):
                self.verifier.verify_maps(changed,Path(record["source_root"]),Path(record["build_root"]),
                    Path(record["install_root"]),parent,common,helpers)


if __name__=="__main__":unittest.main()
