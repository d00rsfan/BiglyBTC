"""Freeze bounded private source/owner APIs without claiming Host activation."""
from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114';EV='migration/evidence/20261121-private-tag-source-owner';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs=json.loads((D/'verification-inputs.json').read_text());assert len(paths)==13
assert all(sha(N/p)==inputs[p]==sha(D/'native-staged'/p) for p in paths)
for path,h in json.loads((D/'pre-implementation.json').read_text())['source_before_sha256'].items():
 if path.endswith(('manual_tag_store.cpp','prepared_tag_rate_source.cpp')):assert sha(N/path)==h,path
checks=[];qt=0;names=set()
for spec in ['current-gcc-checks.json','current-san-checks.json','current-clang-checks.json','abi1-checks.json','owner-checks.json','owner-san-checks.json','owner-clang-checks.json','native-checks.json','current-checks.json']:
 for task in json.loads((D/spec).read_text()):
  name=task['name'];assert name not in names,name;names.add(name);data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:|(?m:^)(?:PRIVATE_TAG_SOURCE|PRIVATE_TAG_STORAGE) ',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0' and s=='0' for _,f,s in totals),name;qt+=sum(int(p) for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines() if 'PASS' in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
for label in ['native','current']:
 data=(W/f'private-tag-{label}-build.log').read_text(errors='replace');assert not re.search(r'FAILED:|fatal error:|error:|ninja: build stopped',data),label
preserve=json.loads((D.parent/'profile-tag-preadd-20261121/preservation.json').read_text())
for key in ['delivered_daily','selected_sdk','selected_sdk_record','candidate_sdk']:assert sha(Path(preserve[key]['path']))==preserve[key]['sha256'],key
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()==preserve['original_head'];assert sha(R/'.git/index')==preserve['original_index_sha256']
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n')
artifacts={}
for directory in ['public-shared-quota-runtime-20261121/native-gcc','i2p-provider-attachment-20261029/native-followup-20261030/caches/san']:
 base=R/'build'/directory
 for name in ['engine/libbiglybtc_local_engine.a','services/libbiglybtc_owned_i2p_source.a','services/libbiglybtc_i2p_provider_owner.a','app/BiglyBTC','tests/biglybtc-private-tag-source-tests']:
  file=base/name;assert file.exists(),file;artifacts[str(file)]=sha(file)
(D/'qualified-artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir()) if p.is_file()and(p.suffix in ['.py','.json','.cpp','.hpp','.md','.inc']or p.name=='CMakeLists.txt')and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md']and'.publish-state.'not in p.name]
logs=sorted([*W.glob('private-source-*.log'),*W.glob('private-tag-*.log')]);files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for p in recipes:shutil.copy2(p,out/p.name)
 for p in logs:
  target=out/'logs'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files.extend(str(p.relative_to(repo)) for p in sorted(out.rglob('*')) if p.is_file())
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal qualification:{len(checks)} unique runtime groups,{qt} Qt rows,zero failures/skips. Both full application builds pass. Source/controller and actual private owner/storage controls pass per GCC/scoped SAN/Clang;old ABI1 shared-source refusal and existing Native/ordinary behavior pass. No actual tagged private transport or normal Host activation credit.\n'
spec=dict(files=files,subject='Prepare private tag sources and SDK classes before owned storage',evidence_subject='Record explicit private source and owner preparation qualification',evidence=EV,checks=checks,review=review,
 qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_i2p_helper='33d97eca67a9f332e3d4da7e7492ec54f4bec078',native_parent='19d9108b08aedb24d0be7728a5db76421486f6ec',sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution',verification_inputs=inputs,qualified_artifacts=artifacts,log_sha256={c['name']:sha(W/(c['name']+'.log')) for c in checks},runtime_groups=len(checks),qt_rows=qt,variants=['GCC16','scoped GCC16 ASan/UBSan changed controller,SDK wrapper,private source/owner and fixtures;Release dependencies','Clang24 with GCC16 libstdc++','GCC16 selected ABI1 ordinary/refusal'],explicit_private_source_before_storage=True,explicit_check_only_add=True,generic_store_cas_unchanged=True,retained_controller_reference=False,normal_host_activation=False,live_private_source_refresh=False,actual_private_payload_credit=False,default_private_rate_fence_preserved=True,global_shared_owner_connected=False,preservation=preserve,full_sdk_sanitized=False,tsan=False),
 tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update='Explicit private tag source/owner APIs qualified at `@SOURCE@` / `@TREE@`:sealed complete source/actual SDK cohort,transient IO-worker controller,raw full identity before storage,and native check-only constructor binding with ready pause. Actual SDK/source/owner controls pass per GCC/scoped SAN/Clang;generic store CAS and old ABI1/default active fence stay strict. [Review](@EVIDENCE@/REVIEW.md). Continue normal Host common-controller lifetime/admission,live private source/stamp/assignment effects,tagged I2P payload/promotion and common global/LAN owner without stopping. No Host activation/private payload credit or daily promotion.',
 plan_update='2026-11-21: [explicit private tag source and owned storage preparation](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:actual shared SDK/source preparation precedes storage;explicit check-only add retains SDK ready pause. Continue Host/live source/private payload/promotion/global integration without a checkpoint stop;default active fence remains.',components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths),dict(id='services.i2cp_initial_session',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'unique runtime groups;',qt,'Qt rows')
