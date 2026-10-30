from pathlib import Path
import json,hashlib,subprocess,shutil,sys,datetime
R=Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';T=R/'build/i2p-multi-20261030';E=R/'migration/evidence/20261030-i2p-multi-torrent';rel=E.relative_to(R).as_posix();pb,db,sb,ar,sr=sys.argv[1:]
def load(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(v,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git',*args],cwd=N,text=True).strip()
code=load(T/'private'/pb/'build-receipt.json')['source_commit'];assert git('rev-parse','HEAD:preview/native')==git('rev-parse',code+':preview/native')
batches={lane:load(T/lane/b/'result.json') for lane,b in [('private',pb),('default',db),('san',sb)]};assert all(v['status']=='PASS' for v in batches.values());assert len({v['source_map_sha256'] for v in batches.values()})==1
actual={run:load(T/'acceptance/runs'/run/'result.json') for run in [ar,sr]};assert all(v['status']=='PASS_REAL_ISOLATED_ROUTER_MULTI_I2P_HOST_TRANSFER' for v in actual.values())
assert all(v['native']['Host_shared_session_controls'] and v['native']['one_download_limit_handoff'] and v['native']['per_target_counters_preserved'] for v in actual.values())
ci=load(T/'hosted-ci.json');assert ci['status']=='PASS' and ci['source_commit']==code
bundle=load(T/'bundle.json');assert bundle['source_commit']==code
subprocess.run(['python3',str(T/'collect-evidence.py')],check=True)
review=f'''# Shared-session private I2P torrents

The authorised bounded chapter is complete at native `{code}` on main. Several pure I2P torrents share one retained original identity, source authority, I2CP/Streaming manager, bridge and measured provider session. The larger I2P/plugin and queue components remain in progress.

The original saved 75 queued/start intent, 70 stopped/paused intent, force/position inputs and supported queue/rate settings are used. My Torrents controls each admitted torrent independently. A target checkpoint requires its real paused/flush/remove/weak-expiration/executor/essential-alert fences; it does not abort a sibling. Common-source/key/config loss withdraws the group. Trusted original writes preserve unknown/raw metadata and unrelated rows. Intake accepts the supported queued choice with original-compatible journalling. Statistics separates actual per-target and aggregate current I2P traffic from Public/lifetime totals. The I2P library traffic columns also currently show admission traffic; cumulative original per-torrent counters are checkpointed and verified on disk. Merging those totals into the combined library UI remains part of the proposed coexistence chapter.

The source anchor consumes the original private identity once. Children retain exact payload/full-identity claims through retirement. Source reads and joins remain on workers; physical effects require fresh one-shot validation covering the real operation. Additive SDK registration preserves the existing V1 prefix/default policy and uses full hashes plus non-reused per-target admission IDs. Shared serial NAMING completion cannot grant a retired target.

## Validation and fixes

Final-source private GCC, default-OFF GCC and leak-enabled native ASan+UBSan batches passed. Both real isolated router/tracker/UI runs (GCC and instrumented native) transferred two distinct 2 MiB payloads on original inodes, kept a third saved torrent stopped, exercised real per-target Pause/Resume/Stop/Queue, preserved the sibling admission/generation/Host session and progress, joined/checkpointed, reopened the same original identity and restored 75/70/pause state and counters. With max-download 1, exactly one eligible torrent queued, then the stopped running torrent handed its slot to its unchanged sibling. Final actual joins precede payload hashes, counters and profile-lock reacquisition. Exact readbacks cover per-target/global limits and loopback global peer-class membership; they are not a short-sample rate benchmark.

The private SDK has nine fresh direct suites and ten private-owner controls, independently rehashed source/SDK/artifact and 34 same-image exports. Old/default Public approval stays separate. Hosted standard CI passed for the qualified source; exact commands, logs, images, failed attempts and public-only router receipts are retained. Native sanitisation does not instrument the SDK, Qt or Java router. The signed local LeaseSet cache is explicitly warmed: no public discovery, remote tunnel or multihop interoperability claim follows.

A shared-session release defect was found: Bridge discarded its only operation reference when release was not yet possible after Closed/ACK/RX processing. The corrected bridge retains a bounded deferred-release set until actual drain/ACK completion permits release under the existing source authority. Shared capacity is explicit and finite; legacy singleton limits stay unchanged. Regression evidence distinguishes actual release from merely raising a capacity.

The genuine regression also exposed a queue-state defect: a queued intent was treated as proof of an actual pause. The owner now reconciles current provider state with a single pending pause and a real pause alert before Run or retirement; the readiness flag is retained until that fence. An unpaused add with stop_when_ready avoids a redundant post-add resume. Further actual diagnostics exposed premature checked authority: libtorrent can call the extension on_files_checked hook when installing it against metadata, before full verification. Both private owners now mark checked only from the matched real torrent_checked_alert, preventing queue pause during hashing. The exact originating order of the earlier mismatch is not claimed.

Acceptance-fixture errors were corrected without relaxing positive predicates: Java client sets contain Destination objects rather than hashes; caught errors are logged before router shutdown; native hash text case is normalised for exact comparisons; QAction stages advance only after the intended real callback is accepted; fresh generation is checked after actual ready publication. Expanded opt-in diagnostics retain the originating I2CP/bridge error before close and bounded actual SDK status/timer values. The external test tracker was also corrected to retire bounded incomplete EOF/header-timeout connections without request or reply credit, while keeping semantic errors fatal and reporting thread failures immediately. No new upstream Java defect is claimed in this chapter.

## Preview and remaining work

`/home/yurii/Java/BiglyBTC/dist-i2p/BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --private-i2p OWNED_I2CP_PORT`

Qt/dependencies and the approved private provider are bundled; no system install or .deb is required. Bundle manifest `{bundle['manifest_sha256']}`. The caller supplies an isolated original-compatible profile, existing saved private key and owned loopback I2CP router. No replacement identity or Public fallback is generated. The ordinary default dist, running Java application/live profile and original Git HEAD/index are preserved.

This is a bounded preview: 32 native children, 64 active SDK registrations and 128 lifetime admission IDs per owner; transport and identity-ledger bounds remain explicit. Qualification uses two active torrents and one stopped sentinel, not all possible combinations. Unknown scheduler inputs and automatic new-seed positioning remain visibly unavailable. Full queue/plugin/client/performance parity and public/multihop behaviour remain open. Late tracker-result controls are SDK-level; this run does not certify a real delayed remote tracker. No DATA/chat/rooms work was started.

The proposed next chapter is Public + I2P coexistence in one application: preserve each torrent's original network selection, combine library/queue/UI observations and keep the Public and private I2P ownership/traffic boundaries intact. It has not been started.

## Immutable provenance

BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`; official I2P `9134f808337b401e8e53c73734c81fab04280c9d`; libtorrent `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. Modified router JAR SHA256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`. SDK source capsule `63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3`, source/build record `449aa82f665786ce63f4e1778cee23bf07ca45439b762cf283b67efd0d48af0f`, private DSO `7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4`. Full source maps, review/approval and build origins are retained under provider. Component source-coverage integration nulls and first-port baselines are preserved.
'''
(E/'REVIEW.md').write_text(review)
v={'status':'PASS_BOUNDED_SHARED_SESSION_MULTI_I2P_CHAPTER','source_commit':code,'source_tree':git('rev-parse',code+':preview/native'),'builds':{lane:{'batch':b,'status':batches[lane]['status'],'receipt_sha256':sha(T/lane/b/'result.json')} for lane,b in [('private',pb),('default',db),('san',sb)]},'actual_runs':{run:actual[run]['status'] for run in [ar,sr]},'hosted_ci':ci,'bundle':bundle,'stop_after_chapter':True,'next_chapter_started':False,'whole_component_parity':False,'private_keys_published':False};write(E/'verification.json',v)
task=R/'migration/tasks/P07-i2p-multi-torrent.md';task.write_text(f'''# Shared-session multi-torrent private I2P

State: authorised bounded chapter complete at `{code}`; stopped before Public + I2P coexistence. [Review](../evidence/20261030-i2p-multi-torrent/REVIEW.md), [verification](../evidence/20261030-i2p-multi-torrent/verification.json).

One original key/I2CP/Streaming/provider session, independent target admission/retirement/checkpoint and classic controls, compatible saved queue/rate/restart and observed Statistics. Fresh private/default/SAN checks, actual two-swarm controls/restart/handoff on GCC and sanitised native, and hosted CI pass. Legacy singleton bounds remain; shared deferred releases retain actual ACK/RX ownership. Larger component parity stays open, including unavailable queue facts/automatic new-seed positioning, Public coexistence/discovery/multihop and performance. DATA/chat/rooms remain later work. Original Java/live profile/default dist are preserved.
''')
update=R/'migration/updates/services.i2cp_initial_session/20261030-shared-multi-torrent.md';update.write_text(f'''# Shared-session private I2P

Native `{code}` adds bounded per-target admissions and original-source publications, independent real retirement/checkpoints, shared transport capacity/deferred release and saved queue/rate/restart/classic-UI integration. [Full review](../../evidence/20261030-i2p-multi-torrent/REVIEW.md) records all upstream hashes, actual interoperability, sanitiser/default/CI evidence and limitations. No whole-component source-integration baseline is advanced; existing states/nulls remain.
''')
p=R/'migration/upstreams.lock.json';registry=load(p);before=[(c['id'],c['state'],c['source_coverage']) for c in registry['components']];commits=git('rev-list','--reverse','6b40b2bb175e5e665d4c1865a587fe7b34d4cfd3..'+code,'--','preview/native').splitlines()
for c in registry['components']:
 if c['id'] not in ['services.i2cp_initial_session','engine.selected_swarm_admission','engine.queue_policy','ui.classic_statistics_preview']:continue
 for cid in commits:
  if not any(x['commit']==cid for x in c['native_commits']):c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':cid,'purpose':'integration'})
 for key,item in [('evidence',rel+'/verification.json'),('review_records',update.relative_to(R).as_posix()),('task_records',task.relative_to(R).as_posix())]:
  if item not in c[key]:c[key].append(item)
 if c['id']=='services.i2cp_initial_session':
  for target in ['preview/native/services/i2p_provider/profile_owner.cpp','preview/native/services/i2p_provider/profile_owner.hpp']:
   if target not in c['target_paths']:c['target_paths'].append(target)
assert before==[(c['id'],c['state'],c['source_coverage']) for c in registry['components']]
for d in registry['external_dependencies']:
 if d['id']=='bounded-engine-libtorrent-private-original-routing':
  a={'url':'file:///home/yurii/Java/BiglyBT/build/i2p-multi-20261030/provider/candidate-r1/install/lib/libtorrent-rasterbar.so.2.0.12','sha256':'7ffbd4c63d7770168bcbdf8e8c19644e18d1509d57ac148331a3c4bd094310c4'}
  if a not in d['artifacts']:d['artifacts'].append(a)
  patch=E/'provider/amendments/tracker-result-admission/changes.patch';item={'path':patch.relative_to(R).as_posix(),'sha256':sha(patch)}
  if item not in d['patches']:d['patches'].append(item)
  if rel+'/REVIEW.md' not in d['build_recipe']:
   d['build_recipe']+=' SHARED PRIVATE I2P CHILD: complete 16-path additive capsule 63e9925c8f4f3e80168425d22b62ef87b23732eccff8fa1b7714f73f3f84e9c3,1087 source / 293 installed / 284 headers,34 same-image exports, nine fresh SDK suites and ten private controls. Actual build origins, independent review/approval, native and real multi-torrent controls/restart qualification: '+rel+'/REVIEW.md. Private-only development approval; default355 and shipping/full-parity/performance states remain unchanged.'
write(p,registry)
p=R/'migration/FEATURE_MATRIX.md';lines=p.read_text().splitlines();lines=[f'| F06.i2cp_initial_session | Signed I2CP, datagrams, tracker and bounded Streaming/provider owners | [Session](tasks/P07-02b-i2cp-initial-session.md) | In progress; `{code}` qualifies private shared-session multi-torrent controls, original queue/rates/checkpoint/restart, bounded deferred stream release and actual two-swarm slot handoff. Public coexistence/discovery/multihop/full queue/plugin/performance parity remains open. [Review](evidence/20261030-i2p-multi-torrent/REVIEW.md). |' if x.startswith('| F06.i2cp_initial_session |') else x for x in lines];p.write_text('\n'.join(lines)+'\n')
p=R/'MIGRATION_PLAN.md';lines=p.read_text().splitlines();lines[2]='Latest user boundary (2026-10-30): the authorised shared-session multi-torrent private I2P chapter is complete. Stop here; propose Public + I2P coexistence, do not start it. Preserve Java/live profile and main-only milestones. Read [current state](migration/ACTIVE.md).';lines[4]=f'Qualified native source `{code}` adds one original identity/session with independent torrents, original queue/rate/restart and actual joined checkpoints; [review](migration/evidence/20261030-i2p-multi-torrent/REVIEW.md). Full I2P/plugin/queue/performance parity remains open; Public + I2P coexistence and DATA/chat/rooms were not started.';p.write_text('\n'.join(lines)+'\n')
(R/'migration/ACTIVE.md').write_text(f'''# Shared-session private I2P chapter complete; stopped

Authorised chapter complete at `{code}`. [Review](evidence/20261030-i2p-multi-torrent/REVIEW.md), [verification](evidence/20261030-i2p-multi-torrent/verification.json). Shared original identity/session, independent controls/checkpoints, saved queue/rates/restart and real two-swarm slot handoff qualify within the documented scope. Preview `/home/yurii/Java/BiglyBTC/dist-i2p/BiglyBTC` requires explicit isolated profile/router options.

Proposed next: Public + I2P coexistence in one application; NOT started. Whole epic/larger component parity is incomplete. Preserve Java/live profile, original HEAD/index/default dist. Questions: none. Follow [resource policy](RESOURCE_POLICY.md): reuse current caches, clean obsolete generated files, no build archives. Durable diagnostics `build/i2p-multi-20261030`; cached builds, aggregate3 compiler jobs/nice5, logs on disk.
''')
p=R/'migration/continuation.json';state=load(p);state.update(updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),objective='Authorised shared-session private I2P chapter complete; stop before Public+I2P coexistence.',native_head=git('rev-parse','HEAD'),qualified_native_source=code,native_source_pushed=True,current_source_qualification=v['status'],current_section='P07 shared-session multi-torrent private I2P — complete',next_action='STOP. Describe Public + I2P coexistence as proposed next chapter; do not begin it.',active_local_sessions=[],active_sessions=[],pending_question='None.',stop_after_current_section=True,parallel_source={'error_codec':'SDK and read-only diagnostics complete','error_driver':'source authority/shared transport/release complete','error_oracle':'actual acceptance source complete'},remote_ci=ci);state['receipts'].append({'name':'Shared-session private I2P chapter','path':rel+'/verification.json','sha256':sha(E/'verification.json')});write(p,state)
shutil.copytree(E,N/rel,dirs_exist_ok=True)
for name in ['migration/RESOURCE_POLICY.md','MIGRATION_PLAN.md','migration/ACTIVE.md','migration/continuation.json','migration/FEATURE_MATRIX.md','migration/upstreams.lock.json',task.relative_to(R).as_posix(),update.relative_to(R).as_posix()]:
 dst=N/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(R/name,dst)
print(json.dumps({'documentation':'READY_FOR_VALIDATION','source':code,'evidence_files':sum(1 for p in E.rglob('*') if p.is_file())}))
