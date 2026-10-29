#!/usr/bin/env python3
"""Publish the one checked admitted-tracker preview and record bounded acceptance."""
import ctypes, datetime, hashlib, json, os, pathlib, shutil, subprocess, xml.etree.ElementTree as ET
R=pathlib.Path('/home/yurii/Java/BiglyBT'); N=R.parent/'BiglyBTC'; H=pathlib.Path(__file__).parent
E=pathlib.Path('migration/evidence/20261029-direct-i2p-identity'); out=R/E
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(p): return json.loads(pathlib.Path(p).read_bytes())
def write(p,v): pathlib.Path(p).write_text(json.dumps(v,indent=2)+'\n')
def bundle(p):
 d=load(p/'BUILD_MANIFEST.json')
 assert d['distribution_scope']=='private-qualification-only'
 assert {x.relative_to(p).as_posix():sha(x) for x in p.rglob('*') if x.is_file() and x.name!='BUILD_MANIFEST.json'}==d['files_sha256']
 return d
adoption=load(H/'root-adoption-r2.json'); source=adoption['source_commit']; implementation=adoption['implementation_commit']
assert source=='5abc17533521c881971d4e3f5551ff2963d4c648' and implementation=='5dfbd587e16e86122edd548f861a67430c20586d'
timeout=load(H/'timeout-adoption.json')
assert sha(H/'timeout-adoption.json')==adoption['timeout_adoption_sha256']
assert timeout['source_commit']==source and timeout['implementation_commit']==implementation and timeout['no_cpp_source_changed']
assert timeout['only_changed_path']=='preview/native/tests/CMakeLists.txt' and timeout['user_requested_timeout_seconds']==1000
assert subprocess.check_output(['git','diff','--name-only',implementation,source],cwd=N,text=True).splitlines()==[timeout['only_changed_path']]
private_path=H/'private-focused/runs/r3/result.json'; private=load(private_path)
assert private['status']=='PASS_PRIVATE_DIRECT_AND_SOCKS_HTTPS' and private['source_commit']==source and len(private['actual_tests'])==2
offline=load(H/'offline-wrapper-r2/result.json')
assert offline['status']=='PASS_ACTUAL_I2P_IDENTITY_OFFLINE' and offline['source_commit']==implementation and offline['cases_per_reference']==4
assert offline['source_unchanged'] and len(offline['actual_reference_origins'])==2 and not offline['connected_session']
assert offline['bridge_manifest_sha256']=='bf062ca183b92365632ba046c6402b5695990f5d4f3751fd16fbf9f7b87e57a3'
for name,pin in offline['files_sha256'].items(): assert sha(H/'offline-r2'/name)==pin
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip()==source
assert not subprocess.check_output(['git','status','--porcelain'],cwd=N)
for name,row in adoption['source_paths'].items(): assert sha(N/name)==row['after_sha256']
lanes={}
for lane in ('gcc','clang','san'):
 p=H/'runs'/(lane+'-r3'); prior=H/'runs'/(lane+'-r1'); failed_config=H/'runs'/(lane+'-r2'); d=load(p/'result.json'); old=load(prior/'result.json')
 assert d['status']=='PASS_HTTP_TIMEOUT_REPAIR_IDENTICAL_IMAGES' and d['source_commit']==source and d['code_build_source_commit']==implementation
 assert d['source_unchanged'] and d['images_unchanged'] and not d['cpp_builds_executed'] and not d['provider_rebuilt']
 assert d['runner_sha256']==sha(H/'repair-r3.py') and d['source_receipt_sha256']==sha(H/'root-adoption-r2.json')
 assert old['status']=='FAIL' and old['source_commit']==implementation
 config=load(failed_config/'result.json'); assert config['status']=='FAIL' and config['source_commit']==source and not config['cpp_builds_executed']
 assert not (failed_config/'tests.xml').exists(), 'Configuration-only failed repair unexpectedly executed tests'
 expected={'queue-policy','managed-queue','mainline-single-http','pure-i2p-datagram-codec','pure-i2p-client-identity'} | (set() if lane=='clang' else {'mainline-single-tracker-owner'})
 reused=sorted(expected-{'mainline-single-http'})
 assert d['actual_suites']==1 and d['suite_names']==['mainline-single-http'] and d['qualified_suites']==sorted(expected) and d['reused_passed_suites']==reused
 assert private['focused_results'][lane]['sha256']==sha(p/'result.json')
 audit=load(p/'reuse-audit.json'); assert d['reuse_audit_sha256']==sha(p/'reuse-audit.json')
 assert audit['prior_source_commit']==implementation and audit['current_source_commit']==source and audit['all_built_images_identical'] and audit['reused_passed_suites']==reused
 assert audit['only_changed_source']==timeout['only_changed_path']
 for name,pin in audit['prior_evidence_sha256'].items(): assert sha(prior/name)==pin
 assert d['tests_xml_sha256']==sha(p/'tests.xml') and d['full_ctest_log_sha256']==sha(p/'ctest-full.log') and d['commands_sha256']==sha(p/'commands.json')
 rows=list(ET.parse(prior/'tests.xml').getroot().iter('testcase'))
 assert len(rows)==len(expected) and {r.get('name') for r in rows}==expected
 assert {r.get('name') for r in rows if r.find('failure') is not None}=={'mainline-single-http'}
 assert not any(r.find(tag) is not None for r in rows for tag in ('error','skipped'))
 assert all(r.get('status')=='run' and r.find('failure') is None for r in rows if r.get('name') in reused)
 fresh=list(ET.parse(p/'tests.xml').getroot().iter('testcase'))
 assert len(fresh)==1 and fresh[0].get('name')=='mainline-single-http' and fresh[0].get('status')=='run'
 assert not any(fresh[0].find(tag) is not None for tag in ('failure','error','skipped'))
 old_source=load(prior/'source-before.json')['files_sha256']; current_source=load(p/'source-before.json')['files_sha256']
 assert set(old_source)==set(current_source) and {name for name in old_source if old_source[name]!=current_source[name]}=={timeout['only_changed_path']}
 assert old_source[timeout['only_changed_path']]==timeout['before_sha256'] and current_source[timeout['only_changed_path']]==timeout['after_sha256']
 images=load(p/'images-before.json'); assert images==load(prior/'images-before.json') and len(images)==9
 for image in images.values(): assert sha(image['path'])==image['sha256']
 if lane=='gcc':assert images['biglybtc-i2p-client-identity-tests']['sha256']==offline['native_test_elf_sha256']
 lanes[lane]={'actual_fresh_suites':1,'fresh_suite_names':d['suite_names'],'fresh_source_commit':source,
  'reused_passed_suites':reused,'reused_source_commit':implementation,'qualified_suite_count':len(expected),'qualified_suite_names':sorted(expected),
  'compile_only_suites':d['compile_only_suites'],'sanitizer_environment':d['sanitizer_environment'],
  'reuse_audit_sha256':sha(p/'reuse-audit.json'),'fresh_result_sha256':sha(p/'result.json'),'prior_failed_result_sha256':sha(prior/'result.json')}
assert (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
candidate=pathlib.Path(private['candidate']); dist=N/'dist'; assert sha(candidate/'BUILD_MANIFEST.json')==private['candidate_manifest_sha256']
new=bundle(candidate); old=bundle(dist); assert sha(dist/'BUILD_MANIFEST.json')==private['old_dist_unchanged']
for p in pathlib.Path('/proc').glob('[0-9]*/maps'):
 try: maps=p.read_text()
 except (OSError,PermissionError): continue
 assert str(dist)+'/' not in maps and str(candidate)+'/' not in maps, 'Preview still mapped by '+str(p)
libc=ctypes.CDLL(None,use_errno=True); exchange=libc.renameat2; exchange.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
assert exchange(-100,os.fsencode(dist),-100,os.fsencode(candidate),2)==0, os.strerror(ctypes.get_errno())
assert bundle(dist)==new and bundle(candidate)==old
shutil.rmtree(candidate)
removed=[]
for target,image in private['copied_images'].items():
 p=private_path.parent/target; assert sha(p)==image['sha256']; removed.append({'path':str(p),'sha256':sha(p)});p.unlink()
shutil.rmtree(private_path.parent/'test-support')
promotion={'status':'PROMOTED_ONE_CHECKED_DEVELOPMENT_DIST','source_commit':source,'manifest_sha256':sha(dist/'BUILD_MANIFEST.json'),'leaf_count':len(new['files_sha256']),'private_result_sha256':sha(private_path),'previous_dist_removed':True,'previous_dist_retained':False,'atomic_exchange':True,'copied_test_images_removed':removed,'test_support_removed':True,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};write(H/'promotion.json',promotion)
out.mkdir(parents=True,exist_ok=False);copied={}
def copy(p,name):
 dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);copied[name]=sha(dest)
for name in ('root-adoption.json','root-adoption-r2.json','timeout-adoption.json','timeout-afterimage-association.json','run.py','repair.py','repair-r3.py','finish.py','promotion.json'):copy(H/name,name)
for name in ('finish-before.py','changes.patch','review.json','root-correction.json'):copy(H/'finish-adaptation-r3'/name,'finisher/'+name)
copy(H/'manifest.json','manifest.json')
refs={
 'direct-review':R/'build/mainline-http-direct-20261029-review/amendments/adopted-core-and-diagnostics/manifest.json',
 'identity-review':R/'build/i2p-client-identity-20261029-review/manifest.json',
 'identity-manifest':R/'build/i2p-client-identity-20261029/amendments/offline-fixture-bridge/manifest.json',
 'identity-bridge-corrected':R/'build/i2p-client-identity-20261029/amendments/concrete-offline-reader/manifest.json',
 'identity-bridge-factory-proof':R/'build/i2p-client-identity-20261029/amendments/concrete-offline-reader/source-proof.json',
 'identity-bridge-review':H/'offline-review/amendments/concrete-factory-and-gates/manifest.json',
 'identity-bridge-review-detail':H/'offline-review/amendments/concrete-factory-and-gates/review.json',
 'identity-source-pins':R/'build/i2p-client-identity-20261029/source-pins.json',
 'identity-retained-source':R/'build/i2p-client-identity-20261029/retained-identity-source.json',
 'reducer-review':R/'build/admitted-http-followup-20261029/review-ineligible-announces/manifest.json',
}
for label,p in refs.items():copy(p,label+'.json')
old=R/'build/admitted-http-followup-20261029'
for name in ('root-adoption.json','diagnostic-adoption.json','core-fix-adoption.json','run.py','diagnose.py','confirm-fix.py'):
 copy(old/name,'earlier/'+name)
for lane in ('gcc','clang','san'):
 for name in ('result.json','commands.json','tests.xml','ctest-full.log'):
  copy(old/'runs'/(lane+'-r1')/name,'earlier/'+lane+'-r1/'+name)
for run in ('clang-diagnostic-r1','clang-corefix-r1'):
 for name in ('result.json','commands.json','selected.xml','selected.txt'):copy(old/'runs'/run/name,'earlier/'+run+'/'+name)
for name in ('offline.py','offline-r2.py'):copy(H/name,'offline/'+name)
for attempt in ('r1','r2'):
 for p in sorted((H/('offline-wrapper-'+attempt)).iterdir()):
  if p.is_file():copy(p,'offline/wrapper-'+attempt+'/'+p.name)
 for p in sorted((H/('offline-'+attempt)).rglob('*')):
  if p.is_file() and 'classes' not in p.relative_to(H/('offline-'+attempt)).parts:
   copy(p,'offline/run-'+attempt+'/'+p.relative_to(H/('offline-'+attempt)).as_posix())
for name in ('OFFLINE-COMMANDS.sh','interop/ClientIdentityVectors.java','parent-delta.patch','reference-api-check.json'):
 copy(R/'build/i2p-client-identity-20261029/amendments/concrete-offline-reader'/name,'offline/corrected-bridge/'+name)
for lane in lanes:
 for attempt in ('r1','r2','r3'):
  # All produced top-level result/source/image/command/XML/log receipts; no owned
  # HOME/cache/profile tree, ELF, or invented missing r2 test outputs are copied.
  p=H/'runs'/(lane+'-'+attempt)
  for receipt in sorted(p.iterdir()):
   if receipt.is_file() and receipt.suffix in ('.json','.xml','.log','.txt'):
    copy(receipt,'runs/'+lane+'-'+attempt+'/'+receipt.name)
for name in ('run.py','run-r2.py','source-review.json','timeout-repair-child/manifest.json','runs/r3/result.json','runs/r3/commands.json','runs/r3/selected-1.xml','runs/r3/selected-1.txt','runs/r3/selected-2.xml','runs/r3/selected-2.txt'):
 copy(H/'private-focused'/name,'private/'+name)
copy(dist/'BUILD_MANIFEST.json','dist-manifest.json')
write(out/'verification.json',{'status':'PASS_BOUNDED_DIRECT_I2P_IDENTITY','source_commit':source,'implementation_commit':implementation,'source_path_count':10,'qualification_scope':'FRESH_HTTP_ONE_PER_LANE_PLUS_EXPLICIT_IDENTICAL_IMAGE_SAME_IMPLEMENTATION_REUSE','actual_fresh_suites_per_lane':{'gcc':1,'clang':1,'san':1},'reused_passed_suite_counts':{'gcc':5,'clang':4,'san':5},'qualified_suite_counts':{'gcc':6,'clang':5,'san':6},'offline_native_source_commit':implementation,'offline_source_association':'SAME_IDENTITY_ELF_AND_CMAKE_ONLY_TIMEOUT_CHANGE','failed_r1_and_configure_only_r2_preserved':True,'lanes':lanes,'actual_private_runnable_rows':2,'actual_offline_cases_per_reference':4,'offline_reference_count':2,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'dist_manifest_sha256':promotion['manifest_sha256'],'prior_dist_retained':False,'real_profile_used':False,'public_network_activated':False,'full_feature_or_performance_parity_claimed':False,'artifacts_sha256':copied})
(out/'REVIEW.md').write_text(f"""# Regular tracker authority and offline I2P identity ownership

Qualified current source {source}, implementation {implementation}; ten exact new working/committed afterimages over the independently reviewed admitted tracker implementation and its reducer correction. All source/review pins are retained. The original failed GCC/Clang/SAN HTTP runs are preserved, not credited as current passes.

Regular HTTP(S) requests now have an explicit freshly authenticated role independent of Mainline permission. Public+Tracker saved/private/DHT-disabled rows qualify through DIRECT or the configured SOCKS5 route. Current Mainline-owned candidates are excluded; one effective HTTP URL per admitted row is supported. A retained same-OFD profile token preserves genuine regular authority across optional Mainline close and releases after owner joins. No new admission, peer/DHT right, duplicate libtorrent scrape or timer is introduced.

The actual new admitted HTTPS row failed in all three original runs. A bounded single-row diagnostic measured one scrape, no TLS error and Unknown shared state. An independently proven original-semantics defect applied failed/inapplicable announces before checking applicability, erasing scraper state or advancing its chronological watermark. The guarded reducer correction preserves the scraper on those announces while retaining the original downloaded-only and signed-int boundaries. Changing only the two reviewed reducer/unit files made the same diagnostic HTTPS row pass. The later combined tests remain separate actual runs.

At implementation {implementation}, GCC/Clang/SAN built nine affected products and their initial r1 CTest runs passed five/four/five named suites; only mainline-single-http hit its former 240-second CTest budget. Those failed overall results, passed row XML, logs, source and image receipts remain intact. Current source {source} changes only that CMake timeout to the user-requested 1000 seconds. The r2 repair attempt configured successfully but its bracket-quoting verifier failed before any HTTP or C++ execution. Corrected repair-r3 configured and freshly passed exactly ONE HTTP suite per lane without compiling or changing any of the nine ELFs. Explicit source-delta and image-equality audits qualify six/five/six unique suites using fresh HTTP plus the exact earlier five/four/five passes; they do not claim fresh six/five/six runs or a new full-suite exit. Clang single-owner remains compile-only. No unrelated Tor/DHT/network-policy/launcher/provider replay is pooled.

The pure I2P identity library imports/owns/exports the original online Enc0 signing0/7 private stream, validates signing-key possession, cleanses owned storage and signs through the qualified datagram codec. Enc0 decryption bytes remain opaque as in the original; no claim of encryption-key/public-key correspondence. The first Java bridge attempt failed at javac because the original reader base is abstract; its logs are retained. The corrected public factory invokes the genuine concrete unconnected reader. Both exact official and modified Java cores then actually read/exported and verified four cases each in offline-r2, using the implementation {implementation} native identity ELF; class origins and both-direction key bytes/signatures plus negative checks were measured. That exact ELF is unchanged under the CMake-only current source, with no repeat of Java or invented new source origin. No connected session, router, profile discovery/writer or complete modified-core equivalence.

One private candidate then executed exactly two admitted HTTPS/JKS rows, DIRECT and SOCKS5. Actual private QtCore/Network/TLS/provider and system OpenSSL initialization were checked. It replaced dist atomically; prior dist and copied test images were deleted. The running original client, real profile and payload were untouched. No public-network, whole-client or comparative performance claim.

Open work includes DIRECT regular UDP/mixed routes, active multi-URL behavior, separate tracker/data proxies and broader proxy/auth/TLS parity; I2CP/session/stream/router integration and unsupported identity algorithms/offline formats remain open. Whole-component integration baselines remain in_progress/null.
""")
lock=load(R/'migration/upstreams.lock.json')
for c in lock['components']:
 if c['id'] not in ('engine.queue_policy','engine.mainline_queued_scrape','engine.tracker_scrape_observations','ui.classic_navigation_preview'):continue
 c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':implementation,'purpose':'integration'})
 c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
T=R/'build/i2p-client-identity-20261029/tracking-proposal'
assert sha(T/'manifest.json')=='f0ddc76c09f0aa9d5ded4f068bb4e7de7455a1090eab57b472668d2f54c977c2'
for name,pin in load(T/'manifest.json')['artifacts_sha256'].items():assert sha(T/name)==pin
new_component=load(T/'registry-additions.json')['components'][0]
assert new_component['id'] not in {c['id'] for c in lock['components']}
assert new_component['native_commits'][0]['commit']=='PLACEHOLDER';new_component['native_commits'][0]['commit']=implementation
new_component['review_records'].append((E/'REVIEW.md').as_posix());lock['components'].append(new_component)
for root in (R,N):
 write(root/'migration/upstreams.lock.json',lock)
 row=(T/'feature-row.txt').read_text().replace('compiler/runtime acceptance pending.','current GCC/Clang/SAN and actual four-case offline official+modified identity bridge passed; see evidence/20261029-direct-i2p-identity/verification.json.')
 matrix=root/'migration/FEATURE_MATRIX.md';assert 'F06.i2p_client_identity' not in matrix.read_text();matrix.write_text(matrix.read_text()+'\n'+row)
 task=root/'migration/tasks/P07-02a-i2p-datagram.md'
 task.write_text(task.read_text()+f"\nOnline identity prerequisite services.i2p_client_identity, implementation {implementation}, qualified source {source}: native identity suite passed at implementation and is explicitly reused with identical images after a CMake-only timeout change on GCC/Clang/SAN; actual offline official and modified Java import/export/signature cases each passed four rows. Evidence: evidence/20261029-direct-i2p-identity/verification.json. Component remains in_progress with null source integration baselines; no connected session, router or profile writer.\n")
 review=root/'migration/updates/services.i2p_client_identity/20261029-bounded-in-progress.md';assert not review.exists();review.parent.mkdir(parents=True,exist_ok=True)
 text=(T/'staging/migration/updates/services.i2p_client_identity/20261029-bounded-in-progress.md').read_text().replace('PLACEHOLDER',implementation).replace('Runtime acceptance pending.','Current GCC/Clang/SAN and actual offline four-case-per-reference acceptance passed; see migration/evidence/20261029-direct-i2p-identity/verification.json.')
 review.write_text(text+f'\nQualification addendum: actual native implementation {implementation}; current qualified source {source} differs only in the HTTP CTest timeout. Identity native tests originate at the implementation; the exact image is unchanged. Offline-wrapper-r2 passed four actual cases per official/modified reference using that implementation ELF and corrected concrete factory bf062; failed Java r1 remains retained. Earlier pending statements above describe the initial source-only review, not the current acceptance. See migration/evidence/20261029-direct-i2p-identity/verification.json. Component and both source integration baselines remain in_progress/null.\n')
shutil.copytree(out,N/E)
print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'],'fresh HTTP1/1/1 + explicit unchanged-image prior5/4/5 => qualified6/5/6, private2, offline4x2; prior dist deleted')
