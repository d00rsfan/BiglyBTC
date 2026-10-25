from pathlib import Path
import hashlib,json,subprocess
w=Path(__file__).resolve().parent;c=w/'remainder/reordered-completion';s=w/'engine/staging';h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((c/'manifest.json').read_text());assert h(c/'manifest.json')=='e8665777d8792366ad85c1f298241f8278a473edd5c4b69eefd1225df73c5baa'
for rel,sha in m['artifact_sha256'].items():assert h(c/rel)==sha,rel
for rel,sha in m['before_sha256'].items():assert h(s/rel)==sha,rel
managed=s/'preview/native/engine/managed_storage.cpp';assert h(managed)==m['managed_storage_preimage_sha256']
for name in ['completion.patch','managed-storage.patch']:
 for dry in [True,False]:
  cmd=['patch','--batch','--forward','--no-backup-if-mismatch','-p1','-i',str(c/name)]+(['--dry-run'] if dry else [])
  r=subprocess.run(cmd,cwd=s,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);assert r.returncode==0,r.stdout
for rel,sha in m['source_sha256'].items():assert h(s/rel)==sha,rel
assert h(managed)==m['managed_storage_expected_after_sha256']
print('Composed6exact completion paths with preserved preimages and validated resulting hashes')
