#!/usr/bin/env python3
"""Select one of two exact independently qualified SDK closures."""
import argparse,hashlib,importlib.util,sys
from pathlib import Path
sys.dont_write_bytecode=True
def select(path):
 digest=hashlib.sha256(Path(path).read_bytes()).hexdigest()
 if digest=="8aa1186254a94da88de30fe002e58928b1d5ed52a179f46046047090456d7b83":name="verify_runtime_manifest_shared_roles_legacy.py"
 elif digest=="a5448526308d37a23afd18464b179e7fee5c36669f7d6353a306fc8c864e9766":name="verify_runtime_manifest_original_rate_policy.py"
 else:raise RuntimeError("Unknown exact SDK role manifest")
 spec=importlib.util.spec_from_file_location("selected_shared_sdk_roles",Path(__file__).with_name(name));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(args):return select(args.manifest).inspect(args)
if __name__=="__main__":
 try:
  p=argparse.ArgumentParser()
  for name in ["manifest","approval","library","include","header","receipt"]:p.add_argument("--"+name,type=Path,required=True)
  p.add_argument("--approval-sha256",required=True);args=p.parse_args();module=select(args.manifest);module.main()
 except Exception as e:print("Shared SDK role admission rejected:",e,file=sys.stderr);sys.exit(1)
