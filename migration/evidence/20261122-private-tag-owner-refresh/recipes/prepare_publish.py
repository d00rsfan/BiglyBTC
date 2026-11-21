from pathlib import Path
import hashlib,json,re,shutil
D=Path(__file__).resolve().parent;R=D.parents[1];N=R.parent/'BiglyBTC';W=R/'build/public-nat-20261114';ev='migration/evidence/20261122-private-tag-owner-refresh';E=R/ev;E.mkdir(parents=True,exist_ok=True);(E/'recipes/logs').mkdir(parents=True,exist_ok=True)
checks=[]
for compiler in ['gcc','san','clang']:
 for task in json.loads((D/f'{compiler}-checks.json').read_text()):checks.append({'name':task['name'],'qt':False,'match':'PASS_PRIVATE_OWNER_REFRESH mode='+task['argv'][-1]})
for task in json.loads((D/'native-abi2-checks.json').read_text()):
 checks.append({'name':task['name'],**({'qt':False,'match':'PASS_PRIVATE_OWNER_REFRESH mode='+task['argv'][-1]} if task['name'].split('-')[-1] not in ['source','host','preadd'] else {'qt':True})})
checks.append({'name':'private-owner-refresh-native-abi1-source-selected-final','qt':True})
for c in checks:
 if c['name']=='private-owner-refresh-native-abi2-preadd':c.update(qt=False,match='PASS_PROFILE_PREADD mode=bound assertions=12')
for c in checks:
 data=(W/(c['name']+'.log')).read_text(errors='replace')
 if c['qt']:assert re.findall(r'Totals: (\d+) passed, 0 failed, 0 skipped',data),c['name']
 else:assert c['match'] in data,c['name']
lognames={c['name'] for c in checks}
for pattern in ['private-owner-refresh-*-compile','private-owner-refresh-*-link','private-owner-refresh-native-abi2-affected-build','private-owner-refresh-native-abi2-regression-build']:
 lognames.update(p.stem for p in W.glob(pattern+'.log'))
for name in sorted(lognames):shutil.copy2(W/(name+'.log'),E/'recipes/logs'/(name+'.log'))
for f in D.glob('*.py'):shutil.copy2(f,E/'recipes'/f.name)
for pattern in ['*-checks.json','*-builds.json','*baseline*.json','native-regression-build.json']:
 for f in D.glob(pattern):shutil.copy2(f,E/'recipes'/f.name)
shutil.copy2(D/'owner_refresh_tests.cpp',E/'recipes/owner_refresh_tests.cpp')
(E/'recipes/logs/.gitattributes').write_text('*.log -diff\n')
qualified=['preview/native/'+str(f.relative_to(D/'drafts')) for f in (D/'drafts').rglob('*') if f.is_file()]+['preview/native/tests/private_owner_refresh_tests.cpp','preview/native/tests/CMakeLists.txt']
for path in qualified:
 draft=D/'drafts'/Path(path).relative_to('preview/native')
 if draft.exists():assert (N/path).read_bytes()==draft.read_bytes(),path
old=json.loads((R/'build/profile-common-tag-owner-20261121/source-publish.json').read_text())['qualification'];qualification={key:old[key] for key in ['original_core','original_i2p_helper','sdk_upstream','sdk_upstream_basis']}
qualification.update(native_parent='ae17fa1131e84fb56a750d783c81e82c00066e41',runtime_groups=len(checks),abi1_excluded_optional_methods=["actualPendingMetadataSealsRejectCrossOwnerAndCannotActivateWithoutAuthorization","realMixedStorageSealsBindPendingAndRejectCrossedActualPublicOwners"],source_sha256={p:hashlib.sha256((N/p).read_bytes()).hexdigest() for p in qualified},log_sha256={name:hashlib.sha256((W/(name+'.log')).read_bytes()).hexdigest() for name in sorted(lognames)},explicit_api_only=True,normal_host_activation=False,actual_private_payload_credit=False,scope='Starting service with actual SDK/storage and loopback I2CP holding socket;no real router or payload claim',quiesced_actual_source_workers=2,source_gate_held_across_sdk_wait=False,all_prepared_children_required=True,callback_invocations_at_most=1,failed_refresh_withdraws_source=True,unchanged_or_refused_save_renews_current_source=True,default_private_rate_fence_preserved=True,retained_controller_reference=False,full_sdk_sanitized=False,tsan=False)
review='''# Private tag source transaction on the actual owner

Native @SOURCE@ / tree @TREE@;parent ae17fa1131e84fb56a750d783c81e82c00066e41. Original core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f and original I2P helper/SDK upstream pins remain recorded in qualification.json. Selected SDK and delivered daily are unchanged.

The explicit synchronous service API quiesces both real source readers on their Qt owner,joins their I/O workers outside filesystem gates,and invokes the original creating-worker callback once. The shared controller then refreshes the complete actual SDK/member cohort;the source owner revalidates all unrelated configuration,inventory,metadata,storage and full identities before atomically publishing fresh tag proofs. A no-op or refused original CAS also renews the unchanged source. Failed renewal withdraws the source and does not revive it or hide an actual committed save.

Nine cases pass under GCC,Clang and scoped ASan/UBSan:changed,no-op,stale-store refusal,held source gate,foreign controller,same-profile independent controller,changed metadata,changed key,and interrupted saving. The same nine pass through the integrated Native target. Actual source,Host and constructor-preparation regressions pass with ABI2;selected ABI1-supported source controls also pass without skips. Two optional discovery/Mixed methods were explicitly excluded from that ABI1 lane because those SDK roles are unavailable;their skipped initial run is not qualification credit. All exact check names,log/source hashes and full Original identities are retained in qualification.json. Changed SDK/private/source TUs are scoped instrumentation;SDK/Qt/dependency archives remain Release. No whole-SDK sanitizer or TSan claim.

These Starting-state tests use generated profiles/payloads and a loopback I2CP holding socket. They grant no live router,payload,Host activation,global/LAN or pending-magnet/promotion completion. The generic store and default active-private-rate refusal remain unchanged. Complete those remaining paths in this current tag-rate chapter,then build/verify the requested NEW daily and STOP. This checked API milestone is not the chapter boundary.
'''
update='2026-11-22: [joined private tag source transaction](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@` / `@TREE@`:two source readers join outside FS,one creating-worker CAS,complete source/SDK refresh and strict unrelated metadata guards;GCC/scoped SAN/Clang plus integrated Native and ABI1 controls pass. Explicit APIs only;default private rate fence,SDK and daily unchanged. Finish current whole tag-rate chapter including Host/pending/promotion/Mixed/global/LAN and actual payload,then verified NEW daily and STOP.'
files=qualified+[str(p.relative_to(R)) for p in E.rglob('*') if p.is_file()]
spec=dict(files=files,subject='Refresh private tag source after joined source-worker transactions',evidence_subject='Record private tag source transaction qualification',evidence=ev,checks=checks,review=review,qualification=qualification,tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update=update,plan_update=update,components=[dict(id=key,purpose='implementation',target_paths=qualified) for key in ['library.tag_rate_limits','services.i2cp_initial_session']])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(checks),'unique passing runtime groups and explicit source/evidence files')
