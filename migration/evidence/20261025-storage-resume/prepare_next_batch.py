from pathlib import Path
import hashlib,json,shutil,subprocess
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');s=w/'next-stage';assert not s.exists();shutil.copytree(w/'engine/staging',s);h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=(w/'final-source-commit.txt').read_text().strip();assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=n,text=True).strip()==source
records=[]
def apply(c,name):
 for dry in [True,False]:
  p=subprocess.run(['patch','--batch','--forward','--no-backup-if-mismatch','-p1','-i',str(c/name)]+(['--dry-run']if dry else[]),cwd=s,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);assert p.returncode==0,p.stdout
c=w/'remainder/selection-policy';assert h(c/'manifest.json')=='45da3c7ab86c888777bb6c29f8ab640b46fa20e5d2bd60f8b0143a1380539d25';m=json.loads((c/'manifest.json').read_text())
for x in m['files']:assert h(c/x['path'])==x['sha256'],x['path']
for x in m['sources']:
 rel=x['path'];p=s/rel
 if not p.exists():p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(n/rel,p)
 assert h(p)==x['before_sha256'],rel;apply(c,Path(rel).name+'.patch');assert h(p)==x['after_sha256'],rel
records.append({'capsule':'remainder/selection-policy','sha256':h(c/'manifest.json')})
c=w/'remainder/allocator-tail';assert h(c/'manifest.json')=='499482750f0a58023c6dc27e9324ddc41080ac6ab76f731eab68b273c73993db';m=json.loads((c/'manifest.json').read_text())
for rel,sha in m['artifacts_sha256'].items():assert h(c/rel)==sha,rel
for rel,sha in m['before_sha256'].items():assert h(s/rel)==sha,rel
apply(c,'change.patch')
for rel,sha in m['source_sha256'].items():assert h(s/rel)==sha,rel
records.append({'capsule':'remainder/allocator-tail','sha256':h(c/'manifest.json')})
c=w/'oracle-fileops';assert h(c/'manifest.json')=='e08bf53359cfc7b0887e2a4de911a3a5d688886839936bf4ffa596b577a299ec';m=json.loads((c/'manifest.json').read_text())
for rel,sha in m['files'].items():assert h(c/rel)==sha,rel
for rel,sha in m['source_sha256'].items():
 assert not(s/rel).exists()and not(n/rel).exists();p=s/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(c/m['source_directory']/rel,p);assert h(p)==sha
p=s/'preview/native/tests/CMakeLists.txt';t=p.read_text();assert 'biglybtc-storage-fileops-handoff-driver'not in t;p.write_text(t+'\n'+(c/'Targets.cmake').read_text());records.append({'capsule':'oracle-fileops','sha256':h(c/'manifest.json')})
assert not any(p.suffix in ['.orig','.rej']for p in s.rglob('*')if p.is_file())
(w/'next-stage-inputs.json').write_text(json.dumps({'base_source':source,'native_mutated':False,'capsules':records,'stage':str(s)},indent=2)+'\n');print('Composed next source batch separately; published native source remains unchanged')
