from pathlib import Path
import hashlib,json,subprocess

w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
record=json.loads((w/'adoption-r1.json').read_text())
def git(*args):return subprocess.check_output(['git',*args],cwd=n,text=True).strip()
assert git('rev-parse','HEAD')==record['base'] and git('branch','--show-current')=='main'
assert not git('status','--porcelain'), 'Native source changed before coherent adoption'
for rel,item in record['files'].items():
 assert rel.startswith('preview/native/') and '..' not in Path(rel).parts
 src=Path(item['source']);dest=n/rel
 assert hashlib.sha256(src.read_bytes()).hexdigest()==item['sha256'],('Changed staging',rel)
 assert (hashlib.sha256(dest.read_bytes()).hexdigest() if dest.exists() else None)==item['before_sha256'],('Changed native preimage',rel)
for rel,item in record['files'].items():
 dest=n/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(Path(item['source']).read_bytes())
(w/'adoption-r1-receipt.json').write_text(json.dumps({'adopted':len(record['files']),'base':record['base'],'manifest_sha256':hashlib.sha256((w/'adoption-r1.json').read_bytes()).hexdigest(),'branch':'main'},indent=2)+'\n')
print('Adopted',len(record['files']),'source paths on main; no profile/process access')
