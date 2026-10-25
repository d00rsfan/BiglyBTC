from pathlib import Path
import hashlib,json,shutil
w=Path(__file__).resolve().parent;p=w/'portable';p.mkdir(exist_ok=False);old=w.parent/'fileops-20261024/portable';n=Path('/home/yurii/Java/BiglyBTC');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name in ['qualify.py','promote.py']:
 t=(old/name).read_text();t=t.replace('build/fileops-20261024/portable','build/storage-20261025/portable');(p/name).write_text(t)
shutil.copy2(old/'normal-launcher.txt',p/'normal-launcher.txt')
b=json.loads((old/'baseline.json').read_text());b['previous_dist_manifest_sha256']=sha(n/'dist/BUILD_MANIFEST.json');assert b['previous_dist_manifest_sha256']=='8f0ad95fe64d27962f112129d8a128b6ae1b5414f4a29f826d768d4e73266169'
b['previous_qualified_source']='dda0654715ffa38feea15784a92bf6f00502523e';b['tool_sha256']={str(x):sha(x) for x in [p/'qualify.py',p/'promote.py',n/'preview/migration/tools/stage_preview.py']};(p/'baseline.json').write_text(json.dumps(b,indent=2)+'\n')
r=json.loads((w/'qualified-plan.json').read_text());mapping=dict(zip(r['suites'][:9],r['targets'][:9]));mapping.update({'managed-download':'biglybtc-managed-download-tests','managed-seeding':'biglybtc-managed-seeding-tests','file-operations-host':'biglybtc-file-operations-host-tests'})
a=json.loads((old/'plan.json').read_text());a.update(tests=list(mapping),test_targets=mapping,scope='Mapped original storage, legacy roots/defaults/links and owned engine retirement with private Qt6.12/source-qualified LT. Compiler and actual original-class handoffs retained separately.')
(p/'plan.json').write_text(json.dumps(a,indent=2)+'\n');print('Prepared guarded storage portable qualification and rollback recipe; not executed')
