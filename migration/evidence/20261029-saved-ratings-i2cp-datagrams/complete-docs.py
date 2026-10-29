#!/usr/bin/env python3
"""Record the qualified batch without rerunning any subject."""
import hashlib,json,pathlib,shutil
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';K=pathlib.Path(__file__).resolve().parent
E='migration/evidence/20261029-saved-ratings-i2cp-datagrams';SOURCE='d384a850980f1b6245230ea852d617b11c405e11'
p=json.loads((K/'promotion.json').read_text());assert p['source_commit']==SOURCE and p['previous_dist_removed'] and p['both_registry_preflights_passed']
assert (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
assert hashlib.sha256((N/'dist/BUILD_MANIFEST.json').read_bytes()).hexdigest()==p['manifest_sha256']
for root in (R,N):
 out=root/E
 for name in ('finish-source-review.json','finish-r2-source-review.json','finish-r2.log','complete-docs.py'):shutil.copyfile(K/name,out/name)
 v=json.loads((out/'verification.json').read_text())
 for name in ('finish-source-review.json','finish-r2-source-review.json','finish-r2.log','complete-docs.py'):v['artifacts_sha256'][name]=hashlib.sha256((out/name).read_bytes()).hexdigest()
 (out/'verification.json').write_text(json.dumps(v,indent=2)+'\n')
state=f'''# Migration state

Read [ACTIVE.md](ACTIVE.md) for authoritative continuation. Continue the entire plan autonomously.

Current qualified source `{SOURCE}` adds explicit saved-rating publication/readback and lease-backed outgoing I2P datagrams. Fresh GCC7/Clang5/leak-enabled ASan+UBSan5 suites and private-runtime7 passed. Only GCC built app/headless/smoke and local-rating UI/wiring products; other lanes qualified five affected service/test products. Both pinned Java artefacts exchanged eight send, four status and one payload DTO cases per reference/direction. [Review](evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md).

One dist, manifest `{p['manifest_sha256']}`, {p['leaf_count']} leaves. Predecessor and copied test images were deleted. Ratings remain explicit, single-peer and possibly partial; I2P submission/status certainty remains separate from delivery. Full profile/source/public/backend integration is open. [Profile I/O](tasks/P06-profile-io-executor.md) must close before public/performance qualification; bounded inbound I2P datagrams are the next disjoint prerequisite.

Initial compiler/test failures, the offline report-only prefix correction and exact prior-registry metadata alignment are retained separately. Successful subjects were not replayed for metadata fixes. Both proposed registries passed offline before promotion; states, upstream baselines and dependency counts remain unchanged. Full P05–P13 and release/performance gates remain open.
'''
(R/'migration/STATE.md').write_text(state)
f=R/'migration/FEATURE_MATRIX.md';lines=f.read_text().splitlines()
for i,line in enumerate(lines):
 if line.startswith('| F09.db_mapping |'):lines[i]=f'| F09.db_mapping | Original one-key DB mapping and bounded owned volatile runtime | [P06-28](tasks/P06-28-azureus-db-mapping.md), [runtime](tasks/P06-AZUREUS-VOLATILE-DATABASE.md) | In progress; source `{SOURCE}` adds explicit saved-rating publication and one-peer reads under current profile/identity/permissions. Normal direct receiving and durable versions retained; payload DB volatile. Public/bootstrap/maintenance and [profile I/O](tasks/P06-profile-io-executor.md) remain open. [Review](evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md). |'
 if line.startswith('| F06.i2cp_initial_session |'):lines[i]=f'| F06.i2cp_initial_session | I2CP framing, signed registration, owned LeaseSet2 and outgoing datagrams | [P07-02b](tasks/P07-02b-i2cp-initial-session.md) | In progress; source `{SOURCE}` passes fresh affected three-lane/private suites and both original Java artefact object comparisons. Explicit lease-backed Datagram2/claimed3 submission, bounded nonce/status/deadline handling. Receive/streaming/router/backend remain open; submission/status does not imply delivery. [Review](evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md). |'
 if line.startswith('| F12.rating_local_policy |'):lines[i]=line.replace('actual host writer/DDB/UI pending','explicit one-peer DDB consumer now qualified separately; automatic host/plugin/UI activation pending')
 if line.startswith('| F12.rating_data |'):lines[i]=line.replace('DDB/UI integration pending','explicit one-peer DDB consumer qualified separately; full DDB/UI integration pending')
f.write_text('\n'.join(lines)+'\n')
notes={
'migration/tasks/P06-AZUREUS-VOLATILE-DATABASE.md':f'''\n\n## Explicit saved-rating consumer\n\nSource `{SOURCE}` binds the existing rating key/encoding/local policy to explicit one-admitted-peer publish/read commands with current saved full identity, network/DHT permission, same profile/version owner and bounded final readback. Normal and original ANON0x10 value flags stay typed; ANON is not anonymous routing. Reads are possiblyPartial,64 versus original256. Empty remote values alone allow local fallback; malformed nonempty values do not. Multi-value publication waits for FINAL and exact origin/version/payload/flags/lifetime match within64 values/slices. Current-source/config/cancel/close checks withdraw observations. Fresh7/5/5 suites and private7 passed; no automatic plugin/queue/public/UI activation. See [review](../evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md). Synchronous profile I/O remains an explicit next architecture gate.\n''',
'migration/tasks/P07-02b-i2cp-initial-session.md':f'''\n\n## Lease-backed outgoing datagrams\n\nSource `{SOURCE}` adds explicit Datagram2/claimed3 Send5/Expires36 with current signer/recipient and genuine unexpired LS2 socket enqueue required. CREATED alone remains insufficient. Fixed-header bounded gzip, one pending tracked nonce, immutable initial reliability mode, monotone nonreused nonce, matching ID/status, independent absolute deadline and local-failure certainty are covered. Fresh affected7/5/5 and private7 passed; both actual Java artifacts exchanged8send+4status+1payload cases/reference/direction. Payload31 DTO parsing is not a receive listener. Receive, routing/tunnel/backend/streaming and public/default activation remain open. [Review](../evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md).\n''',
'MIGRATION_PLAN.md':f'''\n\n## 2026-10-29 explicit saved ratings and outgoing I2P datagrams\n\nQualified source `{SOURCE}`: fresh GCC7/Clang5/leak-enabled ASan+UBSan5 affected suites, private7, both original Java artefacts8send+4status+1payload/reference/direction. One dist replaced/deleted its predecessor. Raw failures and metadata-only corrections remain separate; no replay for report fixes. Explicit one-peer ratings and lease-backed outgoing datagrams remain bounded partial P06/P07 integration. [Evidence]({E}/verification.json). Continue bounded inbound I2P and profile-I/O executor work, then the remaining full plan; no chapter stop or full parity/performance claim.\n'''}
for name,text in notes.items():
 with (R/name).open('a') as stream:stream.write(text)
for name in ['migration/STATE.md','migration/FEATURE_MATRIX.md',*notes]:shutil.copyfile(R/name,N/name)
print('DOCS_RECORDED',SOURCE,p['manifest_sha256'])
