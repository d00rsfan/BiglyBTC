"""Freeze checked SDK-first Native bootstrap and explicit bounded evidence."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
EV='migration/evidence/20261121-private-sdk-bootstrap';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs={name:sha(N/name) for name in paths}
assert len(paths)==5 and all(sha(D/'native-staged'/n)==h for n,h in inputs.items())
archives=json.loads((D/'qualified-native-archives.json').read_text());assert all(sha(Path(n))==h for n,h in archives.items())
checks=[];qt_rows=0
for spec in ['final-checks.json','variant-checks.json','native-checks.json','current-checks.json','baseline-receipt-checks.json']:
 for task in json.loads((D/spec).read_text()):
  name=task['name'];data=(W/(name+'.log')).read_text(errors='replace');assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0' and s=='0' for _,f,s in totals),name;qt_rows+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines() if 'PASS' in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
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
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n');(D/'verification-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir()) if p.is_file() and p.suffix in ['.py','.json','.cpp','.md'] and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md'] and '.publish-state.' not in p.name]
logs=sorted(W.glob('private-bootstrap-*.log'));files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for path in recipes:shutil.copy2(path,out/path.name)
 for path in logs:
  target=out/'logs'/path.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files += [str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file()]
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal results:{len(checks)} positive runtime groups;nine modes per GCC/scoped SAN/Clang and {qt_rows} Native/ordinary Qt rows with no failures/skips. Existing SDK deployment and ordinary ABI1 controls pass. The final Native bootstrap build and ordinary application build pass;before-fix defects remain explicitly expected exit1 controls.\n'
spec=dict(files=files,subject='Prepare private SDK outside source gate before payload storage',evidence_subject='Record private SDK bootstrap boundary qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_i2p_helper='33d97eca67a9f332e3d4da7e7492ec54f4bec078',sdk_role_prerequisite='eecd9ace1417e6ac4f5c0c17bbd4e9ca8a81177d',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution',verification_inputs=inputs,release_archive_inputs=archives,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},modes_per_variant=9,variants=['GCC16 actual Native','scoped GCC16 ASan/UBSan three TUs with Release dependencies','Clang24 three TUs with GCC16 libstdc++/Release dependencies'],qt_rows=qt_rows,source_gate_released_at_actual_sdk_constructor=True,sdk_created_before_payload_storage=True,active_private_rate_fence_lifted=False,all_role_tag_rate_census_implemented=False,global_shared_owner_connected=False,preservation=preserve,full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],
 task_update='Private SDK-before-storage bootstrap qualified at `@SOURCE@` / `@TREE@`:actual constructor/other-thread same-OFD gate probe reproduces prior defects;final9 modes per GCC/scoped SAN/Clang cover ordinary/magnet/direct/service,held gate,stale/config-during-constructor,failure and preserved active-rate refusal. Existing Native/ABI1 controls pass. [Review](@EVIDENCE@/REVIEW.md). Continue actual shared private SDK classes,sealed source/cohort preparation and paused attachment,Host all-role effects and global/LAN ownership without stopping;SDK/daily/private active-rate fence unchanged.',
 plan_update='2026-11-21: [private SDK bootstrap boundary](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:SDK creation outside outer FS gate precedes payload and exact config revalidation. Continue actual common private tag classes/source/cohort/Host effects and global integration without a checkpoint stop.',
 components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths),dict(id='services.i2cp_initial_session',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'runtime groups;',qt_rows,'Qt rows')
