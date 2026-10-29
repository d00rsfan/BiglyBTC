#!/usr/bin/env python3
"""Publish the one checked admitted-tracker preview and record bounded acceptance."""
import ctypes, datetime, hashlib, json, os, pathlib, shutil, subprocess
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
adoption=load(H/'root-adoption.json'); source=adoption['source_commit']; private_path=H/'private-focused/runs/r1/result.json'; private=load(private_path)
assert private['status']=='PASS_PRIVATE_DIRECT_AND_SOCKS_HTTPS' and private['source_commit']==source and len(private['actual_tests'])==2
offline=load(H/'offline-wrapper-r1/result.json')
assert offline['status']=='PASS_ACTUAL_I2P_IDENTITY_OFFLINE' and offline['source_commit']==source and offline['cases_per_reference']==4
assert offline['source_unchanged'] and len(offline['actual_reference_origins'])==2
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=N,text=True).strip()==source
assert not subprocess.check_output(['git','status','--porcelain'],cwd=N)
for name,row in adoption['source_paths'].items(): assert sha(N/name)==row['after_sha256']
lanes={}
for lane in ('gcc','clang','san'):
 p=H/'runs'/(lane+'-r1'); d=load(p/'result.json')
 assert d['status']=='PASS_DIRECT_I2P_IDENTITY_COHORT' and d['source_commit']==source and d['source_unchanged'] and d['images_unchanged']
 assert private['focused_results'][lane]['sha256']==sha(p/'result.json')
 for image in load(p/'images-before.json').values(): assert sha(image['path'])==image['sha256']
 lanes[lane]={k:d[k] for k in ('actual_suites','suite_names','compile_only_suites','sanitizer_environment')}
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
for name in ('root-adoption.json','run.py','promotion.json'):copy(H/name,name)
copy(H/'manifest.json','manifest.json')
refs={
 'direct-review':R/'build/mainline-http-direct-20261029-review/amendments/adopted-core-and-diagnostics/manifest.json',
 'identity-review':R/'build/i2p-client-identity-20261029-review/manifest.json',
 'identity-manifest':R/'build/i2p-client-identity-20261029/amendments/offline-fixture-bridge/manifest.json',
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
copy(H/'offline.py','offline.py');copy(H/'offline-wrapper-r1/result.json','offline/result.json');copy(H/'offline-wrapper-r1/commands.json','offline/commands.json')
copy(R/'build/i2p-client-identity-20261029/amendments/offline-fixture-bridge/OFFLINE-COMMANDS.sh','offline/OFFLINE-COMMANDS.sh')
for p in (H/'offline-r1').rglob('*'):
 if p.is_file() and 'classes' not in p.relative_to(H/'offline-r1').parts:copy(p,'offline/run/'+p.relative_to(H/'offline-r1').as_posix())
for lane in lanes:
 for name in ('result.json','commands.json','images-before.json','tests.xml','ctest-full.log'):
  copy(H/'runs'/(lane+'-r1')/name,'runs/'+lane+'-r1/'+name)
for name in ('run.py','source-review.json','runs/r1/result.json','runs/r1/commands.json','runs/r1/selected-1.xml','runs/r1/selected-1.txt','runs/r1/selected-2.xml','runs/r1/selected-2.txt'):
 copy(H/'private-focused'/name,'private/'+name)
copy(dist/'BUILD_MANIFEST.json','dist-manifest.json')
write(out/'verification.json',{'status':'PASS_BOUNDED_DIRECT_I2P_IDENTITY','source_commit':source,'source_path_count':10,'lanes':lanes,'actual_private_runnable_rows':2,'actual_offline_cases_per_reference':4,'offline_reference_count':2,'provider_rebuilt':False,'provider_sha256':private['provider_sha256'],'dist_manifest_sha256':promotion['manifest_sha256'],'prior_dist_retained':False,'real_profile_used':False,'public_network_activated':False,'full_feature_or_performance_parity_claimed':False,'artifacts_sha256':copied})
(out/'REVIEW.md').write_text(f"""# Regular tracker authority and offline I2P identity ownership

Qualified current source {source}; ten exact new working/committed afterimages over the independently reviewed admitted tracker implementation and its reducer correction. All source/review pins are retained. The original failed GCC/Clang/SAN HTTP runs are preserved, not credited as current passes.

Regular HTTP(S) requests now have an explicit freshly authenticated role independent of Mainline permission. Public+Tracker saved/private/DHT-disabled rows qualify through DIRECT or the configured SOCKS5 route. Current Mainline-owned candidates are excluded; one effective HTTP URL per admitted row is supported. A retained same-OFD profile token preserves genuine regular authority across optional Mainline close and releases after owner joins. No new admission, peer/DHT right, duplicate libtorrent scrape or timer is introduced.

The actual new admitted HTTPS row failed in all three original runs. A bounded single-row diagnostic measured one scrape, no TLS error and Unknown shared state. An independently proven original-semantics defect applied failed/inapplicable announces before checking applicability, erasing scraper state or advancing its chronological watermark. The guarded reducer correction preserves the scraper on those announces while retaining the original downloaded-only and signed-int boundaries. Changing only the two reviewed reducer/unit files made the same diagnostic HTTPS row pass. The later combined tests remain separate actual runs.

Current GCC and leak-enabled ASan/UBSan each passed six suites: queue-policy, managed-queue, mainline-single-tracker-owner, mainline-single-http, pure-i2p-datagram-codec and pure-i2p-client-identity. Clang ran the same set except single-owner, which is compile-only. Nine affected products compiled in every lane, including app/headless/smoke. Full outputs, XML and image/command hashes are retained. No historical runtime pooling or unrelated Tor/DHT/network-policy/launcher/provider replay.

The pure I2P identity library imports/owns/exports the original online Enc0 signing0/7 private stream, validates signing-key possession, cleanses owned storage and signs through the qualified datagram codec. Enc0 decryption bytes remain opaque as in the original; no claim of encryption-key/public-key correspondence. Both exact official and modified Java cores actually read/exported and verified four cases each, using the current native test ELF; class origins and both-direction key bytes/signatures plus negative checks were measured. No connected session, router, profile discovery/writer or complete modified-core equivalence.

One private candidate then executed exactly two admitted HTTPS/JKS rows, DIRECT and SOCKS5. Actual private QtCore/Network/TLS/provider and system OpenSSL initialization were checked. It replaced dist atomically; prior dist and copied test images were deleted. Original Java, real profile and payload were untouched. No public-network, whole-client or comparative performance claim.

Open work includes DIRECT regular UDP/mixed routes, active multi-URL behavior, separate tracker/data proxies and broader proxy/auth/TLS parity; I2CP/session/stream/router integration and unsupported identity algorithms/offline formats remain open. Whole-component integration baselines remain in_progress/null.
""")
lock=load(R/'migration/upstreams.lock.json')
for c in lock['components']:
 if c['id'] not in ('engine.queue_policy','engine.mainline_queued_scrape','engine.tracker_scrape_observations','ui.classic_navigation_preview'):continue
 c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':source,'purpose':'integration'})
 c['review_records'].append((E/'REVIEW.md').as_posix());c['evidence'].append((E/'verification.json').as_posix())
T=R/'build/i2p-client-identity-20261029/tracking-proposal'
assert sha(T/'manifest.json')=='f0ddc76c09f0aa9d5ded4f068bb4e7de7455a1090eab57b472668d2f54c977c2'
for name,pin in load(T/'manifest.json')['artifacts_sha256'].items():assert sha(T/name)==pin
new_component=load(T/'registry-additions.json')['components'][0]
assert new_component['id'] not in {c['id'] for c in lock['components']}
assert new_component['native_commits'][0]['commit']=='PLACEHOLDER';new_component['native_commits'][0]['commit']=source
new_component['review_records'].append((E/'REVIEW.md').as_posix());lock['components'].append(new_component)
for root in (R,N):
 write(root/'migration/upstreams.lock.json',lock)
 row=(T/'feature-row.txt').read_text().replace('compiler/runtime acceptance pending.','current GCC/Clang/SAN and actual four-case offline official+modified identity bridge passed; see evidence/20261029-direct-i2p-identity/verification.json.')
 matrix=root/'migration/FEATURE_MATRIX.md';assert 'F06.i2p_client_identity' not in matrix.read_text();matrix.write_text(matrix.read_text()+'\n'+row)
 task=root/'migration/tasks/P07-02a-i2p-datagram.md'
 task.write_text(task.read_text()+f"\nOnline identity prerequisite services.i2p_client_identity, source {source}: current native suite passed on GCC/Clang/SAN; actual offline official and modified Java import/export/signature cases each passed four rows. Evidence: evidence/20261029-direct-i2p-identity/verification.json. Component remains in_progress with null source integration baselines; no connected session, router or profile writer.\n")
 review=root/'migration/updates/services.i2p_client_identity/20261029-bounded-in-progress.md';assert not review.exists();review.parent.mkdir(parents=True,exist_ok=True)
 text=(T/'staging/migration/updates/services.i2p_client_identity/20261029-bounded-in-progress.md').read_text().replace('PLACEHOLDER',source).replace('Runtime acceptance pending.','Current GCC/Clang/SAN and actual offline four-case-per-reference acceptance passed; see migration/evidence/20261029-direct-i2p-identity/verification.json.')
 review.write_text(text)
shutil.copytree(out,N/E)
print('PROMOTED_AND_RECORDED',source,promotion['manifest_sha256'],'actual suites6/5/6, private2, offline4x2; prior dist deleted')
