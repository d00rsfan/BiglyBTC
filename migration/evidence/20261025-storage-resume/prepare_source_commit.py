from pathlib import Path
import hashlib,json,subprocess
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC');expected=json.loads((w/'adoption-r1.json').read_text())['files']
for receipt in sorted(w.glob('amend-*-receipt.json'),key=lambda p:p.stat().st_mtime_ns):expected.update(json.loads((w/receipt.name.replace('-receipt.json','.json')).read_text())['files'])
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=n,text=True).strip()=='b44ec22ebb3dbb6cee3ade23c32a7ad26a373926'
assert subprocess.check_output(['git','branch','--show-current'],cwd=n,text=True).strip()=='main'
assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=n,text=True).strip()
changed={r[3:].decode() for r in subprocess.check_output(['git','status','--porcelain=v1','-z','--untracked-files=all'],cwd=n).split(b'\0') if r};assert changed<=set(expected),sorted(changed-set(expected))
for rel,x in expected.items():assert hashlib.sha256((n/rel).read_bytes()).hexdigest()==x['sha256'],rel
assert subprocess.run(['git','diff','--check'],cwd=n).returncode==0
p={'branch':'main','parent':'b44ec22ebb3dbb6cee3ade23c32a7ad26a373926','message':'Add original compact and reordered storage IO','paths_sha256':{rel:expected[rel]['sha256'] for rel in sorted(changed)}};(w/'source-commit-plan.json').write_text(json.dumps(p,indent=2)+'\n');print('Reviewed source commit plan:',len(changed),'exact native paths; no commit yet')
