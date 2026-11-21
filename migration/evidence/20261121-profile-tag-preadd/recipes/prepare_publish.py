"""Freeze final pending-controller source,receipts and qualification evidence."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114';EV='migration/evidence/20261121-profile-tag-preadd';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs=json.loads((D/'verification-inputs.json').read_text())
assert len(paths)==5 and all(sha(N/p)==inputs[p]==sha(D/'native-staged'/p) for p in paths)
for name in ['profile_tag_rates.cpp','profile_tag_rates.hpp','profile_prepared_tag_attachment.hpp']:
 assert sha(D/'drafts'/name)==sha(N/'preview/native/engine'/name),name
checks=[];qt=0
for spec in ['final-isolated-checks.json','final-abi1-checks.json','final-native-checks.json','final-current-checks.json','before-checks.json']:
 for task in json.loads((D/spec).read_text()):
  name=task['name'];data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:|(?m:^)(?:PROFILE_PREADD|PROFILE_SHARED_PAYLOAD) ',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0' and s=='0' for _,f,s in totals),name;qt+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines() if 'PASS' in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
for label in ['native','current']:
 data=(W/f'profile-pending-{label}-build.log').read_text(errors='replace')
 assert not re.search(r'FAILED:|fatal error:|error:|ninja: build stopped',data),label
preserve=json.loads((D.parent/'tag-rate-preadd-20261121/preservation.json').read_text())
for key in ['delivered_daily','selected_sdk','selected_sdk_record','candidate_sdk']:
 assert sha(Path(preserve[key]['path']))==preserve[key]['sha256'],key
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()==preserve['original_head'];assert sha(R/'.git/index')==preserve['original_index_sha256']
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
artifacts={}
for directory in ['public-shared-quota-runtime-20261121/native-gcc','i2p-provider-attachment-20261029/native-followup-20261030/caches/san']:
 base=R/'build'/directory
 for name in ['engine/libbiglybtc_local_engine.a','app/BiglyBTC','tests/biglybtc-profile-tag-preadd-tests']:
  file=base/name;assert file.exists(),file;artifacts[str(file)]=sha(file)
(D/'qualified-artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir()) if p.is_file() and (p.suffix in ['.py','.json','.cpp','.hpp','.md','.inc'] or p.name=='CMakeLists.txt') and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md'] and '.publish-state.' not in p.name]
logs=sorted([*W.glob('profile-preadd-*.log'),*W.glob('profile-pending-*.log')]);files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for p in recipes:shutil.copy2(p,out/p.name)
 for p in logs:
  target=out/'logs'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files.extend(str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file())
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal qualification:{len(checks)} runtime groups and {qt} Qt rows,zero failures/skips. Final13 pending controls per GCC/scoped SAN/Clang/ABI1,12 real constructor-only payload scenarios,and both full application builds pass. Three before-fix expected exit1 receipts remain separate. SDK/daily/private fence unchanged.\n'
spec=dict(files=files,subject='Track pending profile tag targets through SDK construction and cancellation',evidence_subject='Record profile pending tag census and retry qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',profile_source_baseline='aae64f96ac4d813e34e88aa0babf3a6cbc9803ec',sdk_preadd_prerequisite='1c6e51910a48b489beeee549a678a3c7f2437eae',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution',verification_inputs=inputs,qualified_artifacts=artifacts,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},variants=['GCC16','scoped GCC16 ASan/UBSan controller,fixtures and fixed D7 wrapper;Release SDK/storage/Qt','Clang24 with GCC16 libstdc++','GCC16 selected ABI1 with exact native SDK wrapper member hashes'],runtime_groups=len(checks),qt_rows=qt,pending_control_modes_per_variant=13,constructor_payload_controls=12,profile_pending_census=True,private_storage_grant=False,private_rate_fence_lifted=False,global_shared_owner_connected=False,preservation=preserve,full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update='Profile pending tag census qualified at `@SOURCE@` / `@TREE@`:paused cross-worker constructor bundles,publish-or-cancel handle harvest,first active source tag and safe dropped-bundle retry. Final13 controls per GCC/scoped SAN/Clang/ABI1 and12 actual two-session payload scenarios pass with strict source/cohort/Host regressions. Three initial exit1 defects retained. [Review](@EVIDENCE@/REVIEW.md). Continue sealed private source/storage permission,actual Host all-role effects and common global/LAN owner without stopping;private rate fence and daily unchanged.',
 plan_update='2026-11-21: [profile pending tag target census](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:constructor handles enter source effects or callbacks cancel;first active tags and dropped preparation retries proven. Continue private source/storage/cohort/Host and global integration without a checkpoint stop.',components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'runtime groups;',qt,'Qt rows')
