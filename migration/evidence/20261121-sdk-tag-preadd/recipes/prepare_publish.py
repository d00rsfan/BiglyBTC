"""Freeze the bounded SDK pre-add milestone after final runtime qualification."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114'
EV='migration/evidence/20261121-sdk-tag-preadd';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((D/'verification-inputs.json').read_text());paths=json.loads((D/'native-source-paths.json').read_text())
assert len(paths)==7 and all(sha(N/p)==inputs[p]==sha(D/'native-staged'/p) for p in paths)
checks=[];qt=0
for spec in ['native-fixture-checks.json','existing-checks.json','payload-checks.json','native-checks.json','current-checks.json']:
 for task in json.loads((D/spec).read_text()):
  name=task['name'];data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:|PREADD_CONTROL |PREADD_EDGE |WORKER_PAYLOAD ',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0' and s=='0' for _,f,s in totals),name;qt+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines() if 'PASS' in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
for label in ['native','current']:
 log=W/f'tag-preadd-{label}-build.log';assert log.exists()
 assert not re.search(r'FAILED:|fatal error:|error:|ninja: build stopped',log.read_text(errors='replace')),label
preserve=json.loads((D.parent/'private-tag-bootstrap-20261121/preservation.json').read_text())
for key in ['delivered_daily','selected_sdk','selected_sdk_record','candidate_sdk']:
 assert sha(Path(preserve[key]['path']))==preserve[key]['sha256'],key
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()==preserve['original_head']
assert sha(R/'.git/index')==preserve['original_index_sha256']
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
archives={}
for name in ['public-shared-quota-runtime-20261121/native-gcc','i2p-provider-attachment-20261029/native-followup-20261030/caches/san']:
 base=R/'build'/name
 for file in ['engine/libbiglybtc_local_engine.a','app/BiglyBTC','tests/biglybtc-sdk-tag-preadd-controls-tests','tests/biglybtc-sdk-tag-preadd-edges-tests']:
  path=base/file
  if not path.exists() and file=='app/BiglyBTC':path=base/'BiglyBTC'
  assert path.exists(),path;archives[str(path)]=sha(path)
(D/'qualified-artifacts.json').write_text(json.dumps(archives,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir()) if p.is_file() and (p.suffix in ['.py','.json','.cpp','.hpp','.md'] or p.name=='CMakeLists.txt') and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md'] and '.publish-state.' not in p.name]
logs=sorted(W.glob('tag-preadd-*.log'));files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for p in recipes:shutil.copy2(p,out/p.name)
 for p in logs:
  target=out/'logs'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files.extend(str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file())
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal qualification:{len(checks)} runtime groups and {qt} Qt rows,zero failures/skips. Both full Native application builds and final constructor fixtures pass. Final6 actual two-session payload controls pass;the one-way drain failure remains excluded and retained.\n'
spec=dict(files=files,subject='Attach prepared tag classes inside paused SDK torrent construction',evidence_subject='Record constructor tag attachment and compatibility qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution',native_prerequisite='aae64f96ac4d813e34e88aa0babf3a6cbc9803ec',verification_inputs=inputs,qualified_artifacts=archives,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},variants=['GCC16','scoped GCC16 ASan/UBSan wrapper and Native fixtures;Release SDK/dependencies','Clang24 with GCC16 libstdc++'],runtime_groups=len(checks),qt_rows=qt,real_two_session_payload_controls=6,existing_control_assertions_per_variant=55,profile_pending_census=False,private_rate_fence_lifted=False,global_shared_owner_connected=False,preservation=preserve,full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update='SDK constructor tag attachment qualified at `@SOURCE@` / `@TREE@`:opaque full-hash/session/epoch one-shot tokens cross a Native worker and attach actual classes before resume. GCC/scoped SAN/Clang controls,common two-session up/down,legacy31/worker24 and current ABI1/Native regressions pass. [Review](@EVIDENCE@/REVIEW.md). Continue ProfileTagRates pending-target census,sealed private source/storage preparation,Host effects and common global/LAN ownership without stopping. Private active-rate refusal and daily unchanged.',
 plan_update='2026-11-21: [SDK constructor tag attachment](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:actual paused pre-add classes,full identities,policy/lifetime revocation and shared payload proven. Continue profile/private source/cohort/Host and global integration without a checkpoint stop.',components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'runtime groups;',qt,'Qt rows')
