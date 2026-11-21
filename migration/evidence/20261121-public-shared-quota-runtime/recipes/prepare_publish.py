"""Freeze explicit Public SDK admission inputs after actual candidate checks."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess

R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC'
D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
EV='migration/evidence/20261121-public-shared-quota-runtime'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((D/'native-source-paths.json').read_text())
checks=[];qt_rows=0
for spec in ['input-checks.json','complete-runtime-checks.json','native-current-checks.json','native-candidate-checks.json']:
 for task in json.loads((D/spec).read_text()):
  name=task['name'];data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0' and s=='0' for _,f,s in totals),name
   qt_rows+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[line for line in data.splitlines() if 'PASS' in line]
   assert markers,name
   checks.append(dict(name=name,qt=False,match=markers[-1]))
assert 'PASS_WORKER_BRIDGE 24' in (W/'public-shared-candidate-worker.log').read_text()
preserve={}
for key,path,expected in [
 ('delivered_daily',R/'build/daily-20261119/preview/BiglyBTC','0b995c5116b56fb3de91a673ce804f84d61465d27bd39f4109e9b6a3d8a0c1b9'),
 ('selected_sdk',R/'build/tag-rate-capacity-20261118/sdk-install/lib/libtorrent-rasterbar.so.2.0.12','065411fcce0f5977b69c1185f722ea36bc568829781fe5df033cf23936c24d7d'),
 ('selected_sdk_record',N/'preview/native/engine/runtime-binding/provider-public-tag-rates-runtime.json','774c4f7521a7e01fae96a3cc44f73ebf08c35fd6584114f1581d6878f6fecddf'),
 ('candidate_sdk',R/'build/tag-rate-cross-owner-20261121/sdk-install/lib/libtorrent-rasterbar.so.2.0.12','05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e')]:
 assert sha(path)==expected,key
 preserve[key]=dict(path=str(path),sha256=expected,bytes=path.stat().st_size)
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()=='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
assert sha(R/'.git/index')=='ac45601f3bed2b3a3d2ac91d715e4b052108a5b7478de62bb67f165ce0de30d4'
preserve.update(original_head='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_index_sha256=sha(R/'.git/index'),real_profile_accessed=False,real_payloads_accessed=False,daily_launchers_changed=False)
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
inputs={name:sha(N/name) for name in source}
for path in D.glob('drafts/*'):
 if path.is_file():inputs[str(path.relative_to(D))]=sha(path)
inputs['provider_probe.cpp']=sha(D/'provider_probe.cpp')
(D/'verification-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
recipes=[]
for path in sorted(D.glob('*.json')):
 if path.name not in ['CONTINUATION.json','source-publish.json'] and '.publish-state.' not in path.name:recipes.append(path)
recipes += [D/'CMakeLists.txt',D/'provider_probe.cpp',D/'relocation_controls.py',D/'shared_quota_runtime_inputs_tests.py',D/'prepare_publish.py']
recipes += [p for p in sorted((D/'drafts').iterdir()) if p.is_file()]
logs=sorted(W.glob('public-shared-*.log'))
files=list(source)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for path in recipes:
  rel=path.relative_to(D);target=out/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
 for path in logs:
  target=out/'logs'/path.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 files.extend(str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file()) if repo==N else None
review=(D/'REVIEW-draft.md').read_text()
review+=f'\nFinal acceptance:{len(checks)} runtime groups passed,including {qt_rows} Qt rows with zero failures/skips. The actual ABI2 Native worker executes all24 consumer-worker controls. Candidate profile/source/assignment,shared UDP IPv4/IPv6 and real Public deployment controls pass. Mapping tests execute155 assertions per GCC/scoped SAN/Clang. Interrupted/timeout/O3 builds are retained as attempts,not passing verification. The resumed O2 build completed without cleaning.\n'
spec=dict(files=files,subject='Admit exact Public shared quota SDK runtime',evidence_subject='Record Public shared quota deployment qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched 2.0.12 archive attribution;not fresh Git-tree comparison',sdk_manifest_sha256='f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081',sdk_artifact_sha256=preserve['candidate_sdk']['sha256'],class_abi='0x4242435300020002',quota_abi='0x4242515300010001',roles=['Public'],exports=59,source_files=1102,installed_headers=290,mapping_assertions_per_variant=155,native_qt_rows=qt_rows,verification_inputs=inputs,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},preservation=preserve,private_admission_lifted=False,normal_host_all_role_census=False,global_shared_budget_connected=False,full_sdk_sanitized=False,tsan=False,selected_sdk_changed=False,daily_changed=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],
 task_update='Exact isolated Public shared-quota SDK deployment qualified at `@SOURCE@` / `@TREE@`:59-export actual loaded-image proof,full1102-source/290-header verification,role/ABI/old-image refusal,actual ABI2 worker/profile/source/assignment/shared UDP/deployment and preserved ABI1 checks pass. [Review](@EVIDENCE@/REVIEW.md). Continue same-image Private/Mixed role qualification,Host all-role/source census and global/LAN ownership without stopping. Daily/current SDK/private active-rate fence unchanged.',
 plan_update='2026-11-21: [exact Public shared-quota runtime](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`;isolated ABI2 Native programs pass actual profile/UDP/deployment controls. Continue same-image Private/Mixed qualification and normal all-role/source/global integration without a checkpoint stop;no SDK/daily promotion.',
 components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=source)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Prepared',len(files),'explicit files;',len(checks),'positive runtime groups;',qt_rows,'Qt rows')
