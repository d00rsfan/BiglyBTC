#!/usr/bin/env python3
"""Read-only exact source snapshot check before/after applying the unified patch."""
import argparse,hashlib,json,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--state',choices=('upstream','final'),required=True);a=p.parse_args()
m=json.loads(Path(__file__).with_name('source-provenance.json').read_bytes());expected=m['upstream']['full_snapshot_sha256'] if a.state=='upstream' else m['final']['full_source_sha256'];actual={}
for directory,dirs,files in os.walk(a.root,followlinks=False):
 for name in dirs:assert not(Path(directory)/name).is_symlink(),'No symlink directories'
 for name in files:
  f=Path(directory)/name;assert not f.is_symlink() and f.is_file();actual[str(f.relative_to(a.root))]=hashlib.sha256(f.read_bytes()).hexdigest()
assert actual==expected,'Source inventory/bytes differ from the measured snapshot'
print('PASS exact source',a.state,len(actual))
