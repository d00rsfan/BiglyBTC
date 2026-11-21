from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114';EV='migration/evidence/20261121-profile-common-tag-owner';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs=json.loads((D/'verification-inputs.json').read_text());assert len(paths)==13
assert all(sha(N/p)==inputs[p]==sha(D/'native-staged'/p)for p in paths)
for path,h in json.loads((D/'pre-implementation.json').read_text())['source_before_sha256'].items():
 if path not in paths:assert sha(N/path)==h,path
checks=[];qt=0;names=set()
for spec in ['gcc-qualified-checks.json','san-qualified-checks.json','clang-qualified-checks.json','native-serial-checks.json','current-checks.json']:
 for t in json.loads((D/spec).read_text()):
  name=t['name'];assert name not in names,name;names.add(name);data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:|(?m:^)(?:COMMON_TAG_OWNER|COMMON_TAG_SERVICE) ',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0'and s=='0'for _,f,s in totals),name;qt+=sum(int(p)for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines()if'PASS'in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
for label in ['native','current']:
 data=(W/f'common-tag-{label}-qualification-build.log').read_text(errors='replace');assert not re.search(r'FAILED:|fatal error:|error:|ninja: build stopped',data),label
preserve=json.loads((D.parent/'private-tag-source-20261121/preservation.json').read_text())
for key in ['delivered_daily','selected_sdk','selected_sdk_record','candidate_sdk']:assert sha(Path(preserve[key]['path']))==preserve[key]['sha256'],key
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()==preserve['original_head'];assert sha(R/'.git/index')==preserve['original_index_sha256']
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
artifacts={}
for directory in ['public-shared-quota-runtime-20261121/native-gcc','i2p-provider-attachment-20261029/native-followup-20261030/caches/san']:
 base=R/'build'/directory
 for name in ['engine/libbiglybtc_local_engine.a','app/libbiglybtc_desktop_host.a','app/BiglyBTC','tests/biglybtc-common-tag-owner-tests']:
  p=base/name;assert p.exists(),p;artifacts[str(p)]=sha(p)
(D/'qualified-artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir())if p.is_file()and p.suffix in ['.py','.json','.cpp','.hpp','.md']and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md']and'.publish-state.'not in p.name]
logs=sorted(W.glob('common-tag-*.log'));files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for p in recipes:shutil.copy2(p,out/p.name)
 for p in logs:
  target=out/'logs'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 for p in sorted((D/'prototype-before-profile-bind-fix').glob('*')):
  target=out/'prototype-before-profile-bind-fix'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files.extend(str(p.relative_to(repo))for p in sorted(out.rglob('*'))if p.is_file())
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal qualification:{len(checks)} unique runtime groups,{qt} Qt rows,zero failures/skips. Final GCC16,Clang24 and scoped GCC16 ASan/UBSan changed controller,normal engine,service and fixtures pass. ASan/UBSan final changed TUs use O0/g1;SDK/Qt/remaining archives are Release. Both full application builds and selected ABI1 new lifecycle tests pass. No full SDK sanitizer or TSan claim.\n'
spec=dict(files=files,subject='Share profile tag controller lifetime across SDK owners',evidence_subject='Record common tag owner lifetime and private service qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_i2p_helper='33d97eca67a9f332e3d4da7e7492ec54f4bec078',native_parent=json.loads((D/'pre-implementation.json').read_text())['native_parent'],sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution;unchanged SDK source/binary',verification_inputs=inputs,qualified_artifacts=artifacts,log_sha256={c['name']:sha(W/(c['name']+'.log'))for c in checks},runtime_groups=len(checks),qt_rows=qt,variants=['GCC16 O2','scoped GCC16 O0 ASan/UBSan changed controller,engine,service and fixtures;Release dependencies','Clang24 O2 with GCC16 libstdc++','selected ABI1 Native lifecycle/regression'],shared_normal_controller_injection=True,actual_startup_profile_guard=True,normal_close_releases_only_own_sdk=True,explicit_synchronous_private_service_api=True,retained_controller_reference=False,normal_host_activation=False,live_private_source_refresh=False,actual_private_payload_credit=False,default_private_rate_fence_preserved=True,global_shared_owner_connected=False,preservation=preserve,full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update='Common profile controller lifetime and explicit private service preparation qualified at `@SOURCE@` / `@TREE@`:actual profile/startup/thread validation before SDK creation;normal close,terminal close and destructor release only their cohort;private service consumes controller synchronously without retained reference. Actual wrong-profile startup defect reproduced before fix;final GCC/scoped SAN/Clang and selected ABI1 lifecycle plus existing Native regressions pass. [Review](@EVIDENCE@/REVIEW.md). Finish live private source/stamp refresh,normal Host/private-only root admission,pending/promotion,Mixed actual tagged payload and common global/LAN as the CURRENT CHAPTER. After complete chapter qualification,prepare the newly requested daily and STOP;this milestone is not the stop boundary. Existing delivered daily remains immutable.',
 plan_update='2026-11-21: [common profile tag controller lifetime](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:normal SDK cohort release preserves shared root;explicit private service preparation retains no controller reference. Finish the current tag-rate chapter including live source/Host/private payload/promotion/global integration;then prepare the newly requested daily and STOP. This milestone is not the chapter boundary.',components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths),dict(id='services.i2cp_initial_session',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'unique runtime groups;',qt,'Qt rows')
