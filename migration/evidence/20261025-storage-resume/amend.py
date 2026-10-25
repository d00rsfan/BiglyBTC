from pathlib import Path
import hashlib,json,subprocess,sys
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
name=sys.argv[1];assert '/' not in name and '..' not in name
manifest=w/(name+'.json');record=json.loads(manifest.read_text())
assert subprocess.check_output(['git','branch','--show-current'],cwd=n,text=True).strip()=='main'
for rel,item in record['files'].items():
 assert (rel.startswith('preview/native/') or rel.startswith('core/')) and '..' not in Path(rel).parts
 src=Path(item['source']);dest=n/rel
 assert hashlib.sha256(src.read_bytes()).hexdigest()==item['sha256'],('Changed staging',rel)
 assert (hashlib.sha256(dest.read_bytes()).hexdigest() if dest.exists() else None)==item['before_sha256'],('Changed preimage',rel)
for rel,item in record['files'].items():
 dest=n/rel
 if dest.exists():
  before=w/name/'before'/rel;before.parent.mkdir(parents=True,exist_ok=True);assert not before.exists();before.write_bytes(dest.read_bytes())
 dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(Path(item['source']).read_bytes())
(w/(name+'-receipt.json')).write_text(json.dumps({'adopted':len(record['files']),'manifest_sha256':hashlib.sha256(manifest.read_bytes()).hexdigest(),'branch':'main'},indent=2)+'\n')
print('Adopted amendment',name,len(record['files']),'paths')
