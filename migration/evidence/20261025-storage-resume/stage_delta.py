from pathlib import Path
import hashlib,json,subprocess,sys
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
name=sys.argv[1];assert '/' not in name and '..' not in name
expected=json.loads((w/'adoption-r1.json').read_text())['files']
for receipt in sorted(w.glob('amend-*-receipt.json'),key=lambda p:p.stat().st_mtime_ns):
 manifest=w/receipt.name.replace('-receipt.json','.json');expected.update(json.loads(manifest.read_text())['files'])
r={'files':{}}
for src in sorted((w/'engine/staging').rglob('*')):
 if not src.is_file():continue
 rel=str(src.relative_to(w/'engine/staging'));dest=n/rel
 before=hashlib.sha256(dest.read_bytes()).hexdigest() if dest.exists() else None
 after=hashlib.sha256(src.read_bytes()).hexdigest()
 if before==after:continue
 if rel in expected:
  pinned=expected[rel]['sha256']
 else:
  original=subprocess.run(['git','show','b44ec22ebb3dbb6cee3ade23c32a7ad26a373926:'+rel],cwd=n,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
  pinned=hashlib.sha256(original.stdout).hexdigest() if original.returncode==0 else None
 assert before==pinned,('Unexpected native preimage',rel)
 r['files'][rel]={'source':str(src.resolve()),'before_sha256':before,'sha256':after}
assert r['files'];manifest=w/(name+'.json');assert not manifest.exists();manifest.write_text(json.dumps(r,indent=2)+'\n')
print('Frozen',name,len(r['files']),'paths')
