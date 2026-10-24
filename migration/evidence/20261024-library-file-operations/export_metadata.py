from pathlib import Path
import hashlib,json,shutil,subprocess
r=Path('/home/yurii/Java/BiglyBT');n=r.parent/'BiglyBTC';w=Path(__file__).resolve().parent;source=(w/'final-source-commit.txt').read_text().strip();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=n,text=True).strip()==source
assert not subprocess.check_output(['git','status','--porcelain'],cwd=n,text=True).strip()
files=['MIGRATION_PLAN.md','migration/STATE.md','migration/upstreams.lock.json','migration/tasks/P03-08-library-file-operations.md','migration/tasks/COMPATIBILITY-FOLLOWUPS.md','migration/updates/library.file_operations/20261024.md'];evidence='migration/evidence/20261024-library-file-operations';assert not (n/evidence).exists()
for name in files:(n/name).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/name,n/name)
shutil.copytree(r/evidence,n/evidence);paths=files+[str(p.relative_to(r)) for p in (r/evidence).rglob('*') if p.is_file()]
assert all(sha(r/p)==sha(n/p) for p in paths)
(w/'metadata-export.json').write_text(json.dumps({'source':source,'files':len(paths),'paths_sha256':{p:sha(n/p) for p in sorted(paths)}},indent=2)+'\n');print('Exported',len(paths),'chapter tracking/evidence files; native source unchanged')
