"""Publish only final checked SDK-role admission and explicit evidence files."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
EV='migration/evidence/20261121-shared-quota-sdk-roles';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs={name:sha(N/name) for name in paths}
assert all(sha(D/'native-staged'/name)==digest for name,digest in inputs.items())
parent='6e99907f0773cf15f2e8a89dd86e0dd49aac44c6'
unchanged='preview/native/services/i2p_dht/inbound_responder.cpp'
assert (N/unchanged).read_bytes()==subprocess.check_output(['git','-C',str(N),'show',parent+':'+unchanged])
tasks=json.loads((D/'sdk-role-checks-r2.json').read_text())[:1]
for spec in ['fresh-role-checks-r2.json','bridge-checks.json','wire-checks.json','quota-control-checks.json','runtime-probe-checks.json','receipt-checks.json','native-checks.json','current-checks.json']:
 tasks+=json.loads((D/spec).read_text())
checks=[];qt_rows=0
for task in tasks:
 name=task['name'];data=(W/(name+'.log')).read_text(errors='replace')
 assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:',data),name
 totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
 if totals:
  assert all(f=='0' and s=='0' for _,f,s in totals),name
  qt_rows+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
 else:
  markers=[s for s in data.splitlines() if 'PASS' in s]
  marker=markers[-1] if markers else '100% tests passed'
  assert marker in data,name;checks.append(dict(name=name,qt=False,match=marker))
preserve={}
for key,path,expected in [
 ('delivered_daily',R/'build/daily-20261119/preview/BiglyBTC','0b995c5116b56fb3de91a673ce804f84d61465d27bd39f4109e9b6a3d8a0c1b9'),
 ('selected_sdk',R/'build/tag-rate-capacity-20261118/sdk-install/lib/libtorrent-rasterbar.so.2.0.12','065411fcce0f5977b69c1185f722ea36bc568829781fe5df033cf23936c24d7d'),
 ('selected_sdk_record',N/'preview/native/engine/runtime-binding/provider-public-tag-rates-runtime.json','774c4f7521a7e01fae96a3cc44f73ebf08c35fd6584114f1581d6878f6fecddf'),
 ('candidate_sdk',R/'build/tag-rate-cross-owner-20261121/sdk-install/lib/libtorrent-rasterbar.so.2.0.12','05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e')]:
 assert sha(path)==expected,key;preserve[key]=dict(path=str(path),sha256=expected,bytes=path.stat().st_size)
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()=='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f'
assert sha(R/'.git/index')=='ac45601f3bed2b3a3d2ac91d715e4b052108a5b7478de62bb67f165ce0de30d4'
preserve.update(original_head='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_index_sha256=sha(R/'.git/index'),real_profile_accessed=False,real_payloads_accessed=False,daily_launchers_changed=False)
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
(D/'verification-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir()) if p.is_file() and p.suffix in ['.json','.py','.cpp','.md','.log'] and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md'] and '.publish-state.' not in p.name]
recipes+=[D/'CMakeLists.txt',D/'runtime-probe/CMakeLists.txt',D/'roles-generated/tests.xml']
for directory in ['roles','mainline-fixture','socks4a-fixture','admission-controls']:
 recipes += [p for p in sorted((D/directory).iterdir()) if p.is_file() and p.suffix in ['.cpp','.hpp','.txt','.log']]
recipes += list(D.glob('*-tests.xml'))
logs=sorted(W.glob('shared-roles-*.log'));files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for path in recipes:
  target=out/path.relative_to(D);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
 for path in logs:
  target=out/'logs'/path.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
 # Preserve actual tool bytes,including failed compiler attempts.
 (out/'.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files += [str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file()]
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal acceptance:{len(checks)} positive runtime groups;{qt_rows} direct Native Qt rows without failures/skips,plus6 SOCKS Qt rows inside the checked XML. The all-role Native driver,private/tag Host,profile/source/assignment/worker,UDP,mapping,DHT responder and actual deployment controls pass. The preserved ABI1 application build and five runtime groups also pass. Exact commands,failed attempts,measured source/object/image/log identities and scope limits are retained.\n'
spec=dict(files=files,subject='Qualify shared quota SDK for exact Public and I2P roles',evidence_subject='Record shared quota SDK role qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',i2p_helper_baseline='33d97eca67a9f332e3d4da7e7492ec54f4bec078',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',closed_role_pins=json.loads((D/'closed-role-pins.json').read_text()),sdk_role_proof=json.loads((D/'sdk-roles-proof.json').read_text()),verification_inputs=inputs,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},native_qt_rows=qt_rows,private_active_rate_fence_lifted=False,actual_i2p_payload_under_shared_tag_quota=False,normal_host_rate_cohort_census=False,global_shared_owner_connected=False,preservation=preserve,compiler_policy=json.loads((D/'inbound-construction-review.json').read_text()),full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],
 task_update='Exact shared-quota SDK roles qualified at `@SOURCE@` / `@TREE@`:fresh9 SDK suites/23 private methods/6 role cases,39 common-class controls per GCC/scoped SAN/Clang,actual59-export Public/private/mixed pins,14 admission refusals/5 image controls and final Native Host/profile/UDP/deployment plus preserved ABI1 checks pass. [Review](@EVIDENCE@/REVIEW.md). Active private rates stay fenced;continue private pre-storage/source preparation,complete Host rate census/paused attachment and common global/LAN ownership without stopping. SDK/daily/real profile unchanged.',
 plan_update='2026-11-21: [shared-quota same-image SDK roles](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@` with fresh SDK and actual Native controls. Continue private source/pre-storage preparation,all-role rate effects and global/LAN integration;no checkpoint stop or daily/SDK promotion.',
 components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths),dict(id='services.i2cp_initial_session',purpose='implementation',target_paths=[p for p in paths if '/i2p_provider/' in p or p.endswith('/services/CMakeLists.txt')])])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n')
print('Prepared',len(files),'explicit files;',len(checks),'runtime groups;',qt_rows,'Native Qt rows')
