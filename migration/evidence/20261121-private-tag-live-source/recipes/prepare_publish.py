from pathlib import Path
import hashlib,json,re,shutil,subprocess
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';D=Path(__file__).resolve().parent;W=R/'build/public-nat-20261114';EV='migration/evidence/20261121-private-tag-live-source';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=json.loads((D/'native-source-paths.json').read_text());inputs=json.loads((D/'verification-inputs.json').read_text());assert len(paths)==4
assert all(sha(N/p)==inputs[p]==sha(D/'native-staged'/p)for p in paths)
checks=[];qt=0;names=set()
for spec in ['gcc-origin-checks.json','san-origin-checks.json','clang-origin-checks.json','abi1-origin-checks.json','native-checks.json','current-checks.json']:
 for t in json.loads((D/spec).read_text()):
  name=t['name'];assert name not in names;names.add(name);data=(W/(name+'.log')).read_text(errors='replace')
  assert not re.search(r'FAIL!|AddressSanitizer:|runtime error:|(?m:^)(?:PRIVATE_TAG_LIVE|PRIVATE_TAG_SOURCE|PRIVATE_TAG_STORAGE|COMMON_TAG_OWNER|COMMON_TAG_SERVICE) ',data),name
  totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',data)
  if totals:
   assert all(f=='0'and s=='0'for _,f,s in totals);qt+=sum(int(p)for p,_,_ in totals);checks.append(dict(name=name,qt=True))
  else:
   markers=[s for s in data.splitlines()if'PASS'in s];assert markers,name;checks.append(dict(name=name,qt=False,match=markers[-1]))
for label in ['native','current']:
 for phase in ['app','test']:
  data=(W/f'private-tag-live-{label}-{phase}-build.log').read_text(errors='replace');assert not re.search(r'FAILED:|fatal error:|error:|ninja: build stopped',data),(label,phase)
preserve=json.loads((D.parent/'profile-common-tag-owner-20261121/preservation.json').read_text())
for key in ['delivered_daily','selected_sdk','selected_sdk_record','candidate_sdk']:assert sha(Path(preserve[key]['path']))==preserve[key]['sha256'],key
assert subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True).strip()==preserve['original_head'];assert sha(R/'.git/index')==preserve['original_index_sha256']
(D/'preservation.json').write_text(json.dumps(preserve,indent=2)+'\n');artifacts={}
for directory in ['public-shared-quota-runtime-20261121/native-gcc','i2p-provider-attachment-20261029/native-followup-20261030/caches/san']:
 for relative in ['engine/libbiglybtc_local_engine.a','app/BiglyBTC','tests/biglybtc-private-tag-live-source-tests']:
  p=R/'build'/directory/relative;artifacts[str(p)]=sha(p)
(D/'qualified-artifacts.json').write_text(json.dumps(artifacts,indent=2)+'\n')
recipes=[p for p in sorted(D.iterdir())if p.is_file()and p.suffix in ['.py','.json','.cpp','.hpp','.md']and p.name not in ['CONTINUATION.json','source-publish.json','REVIEW-draft.md']and'.publish-state.'not in p.name];logs=sorted(W.glob('private-tag-live-*.log'));files=list(paths)
for repo in [R,N]:
 out=repo/EV/'recipes';out.mkdir(parents=True,exist_ok=True)
 for p in recipes:shutil.copy2(p,out/p.name)
 for p in logs:
  target=out/'logs'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 for p in sorted((D/'baseline').glob('*')):
  target=out/'baseline'/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
 (out/'logs/.gitattributes').write_text('*.log whitespace=-blank-at-eol\n')
 if repo==N:files.extend(str(p.relative_to(repo))for p in sorted(out.rglob('*'))if p.is_file())
review=(D/'REVIEW-draft.md').read_text()+f'\nFinal qualification:{len(checks)} unique runtime groups,{qt} Qt rows,zero failures/skips. GCC16 O2,scoped GCC16 O1 ASan/UBSan changed controller/fixture,Clang24 O2 and selected ABI1 clean refusal pass. Both complete Native applications and affected existing source/profile/preadd/owner tests pass. SDK/Qt/remaining archives are Release;no full SDK sanitizer or TSan claim.\n'
base=json.loads((D/'prototype-baseline.json').read_text())
spec=dict(files=files,subject='Refresh private tag evidence for current bound SDK targets',evidence_subject='Record current private tag source and stable owner qualification',evidence=EV,checks=checks,review=review,qualification=dict(original_core='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f',original_i2p_helper='33d97eca67a9f332e3d4da7e7492ec54f4bec078',native_parent=base['native_parent'],sdk_upstream='740a0b9aeabe00e762cc0efe4a0f27593db2550b',sdk_upstream_basis='Inherited matched archive attribution;SDK source/binary unchanged',verification_inputs=inputs,qualified_artifacts=artifacts,log_sha256={c['name']:sha(W/(c['name']+'.log'))for c in checks},runtime_groups=len(checks),qt_rows=qt,actual_bound_target_refresh=True,stable_same_controller_ownership=True,private_source_rebase=False,host_activation=False,private_payload_credit=False,global_lan_complete=False,full_sdk_sanitized=False,tsan=False,preservation=preserve),tasks=['migration/tasks/P08-TAG-RATE-LIMITS.md','migration/STATE.md'],task_update='Current bound private tag source/SDK evidence qualified at `@SOURCE@` / `@TREE@`:actual binding census/full identity,complete current SDK cohort,source epoch renewal without stale pre-add tokens,and weak stable actual-controller ownership.42 new isolated scenarios plus ABI1 clean refusal and Native source/profile/owner regressions pass. [Review](@EVIDENCE@/REVIEW.md). Continue the CURRENT tag-rate CHAPTER:quiesce both private source workers,validate/rebase actual source/stamps,connect Host/private-only/pending/promotion/Mixed and common global/LAN,prove actual tagged private payload. Then prepare the requested NEW daily and STOP. This backend milestone is not the chapter boundary.',plan_update='2026-11-21: [current bound private tag evidence](@EVIDENCE@/REVIEW.md) qualified at `@SOURCE@`:source/SDK policy renewal and stable controller ownership no longer depend on stale constructor tokens. Complete remaining current tag-rate chapter,then verified NEW daily and STOP;no private source rebase/Host/payload completion follows from this milestone.',components=[dict(id='library.tag_rate_limits',purpose='implementation',target_paths=paths)])
(D/'source-publish.json').write_text(json.dumps(spec,indent=2)+'\n');print('Prepared',len(files),'explicit files;',len(checks),'unique groups;',qt,'Qt rows')
