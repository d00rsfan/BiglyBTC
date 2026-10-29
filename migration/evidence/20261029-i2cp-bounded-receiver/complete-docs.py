#!/usr/bin/env python3
"""Record this qualified milestone without any build or test replay."""
import hashlib,json,pathlib,shutil
R=pathlib.Path('/home/yurii/Java/BiglyBT');N=R.parent/'BiglyBTC';H=pathlib.Path(__file__).resolve().parent
E='migration/evidence/20261029-i2cp-bounded-receiver';SOURCE='9fca696181291fd9a1dc7d62942c76d23afbba11'
promotion=json.loads((H/'promotion.json').read_text());assert promotion['source_commit']==SOURCE and promotion['both_registry_preflights_passed'] and promotion['previous_dist_removed']
assert (R/'migration/upstreams.lock.json').read_bytes()==(N/'migration/upstreams.lock.json').read_bytes()
state=f'''# Migration state

Read [ACTIVE.md](ACTIVE.md) for authoritative continuation. Continue the entire plan autonomously.

Current qualified source `{SOURCE}` adds bounded explicit I2CP datagram receiving: legacy Begin/Payload/End and immutable fastReceive, four unread messages/160KiB, typed verified2 versus claimed3, bounded controls/deadlines and queued lifetime-safe notifications. Fresh GCC/Clang/leak-enabled ASan+UBSan five-suite batches and private-runtime five suites passed; both pinned Java artefacts exchanged four new receive/control cases per reference/direction. GCC built app/headless/smoke; other lanes qualified five affected test products. [Review](evidence/20261029-i2cp-bounded-receiver/REVIEW.md).

One dist, manifest `{promotion['manifest_sha256']}`, {promotion['leaf_count']} leaves. Predecessor and copied tests were deleted after atomic promotion. Both proposed registries passed before replacement; component states, upstream baselines and all22 dependencies remain unchanged. Initial Clang test-format warning and the one-newline correction remain separately recorded.

Real-router/backend activation, lookup, streaming, public integration and complete P05–P13 remain open. Bounded profile-I/O executor and an explicit I2P UDP tracker vertical slice are being authored. No full compatibility/performance/release claim.
'''
(R/'migration/STATE.md').write_text(state)
p=R/'migration/FEATURE_MATRIX.md';lines=p.read_text().splitlines()
for i,line in enumerate(lines):
 if line.startswith('| F06.i2cp_initial_session |'):lines[i]=f'| F06.i2cp_initial_session | I2CP framing, signed session, LeaseSet2 and bounded datagrams | [P07-02b](tasks/P07-02b-i2cp-initial-session.md) | In progress; source `{SOURCE}` qualifies bounded legacy/fast receiving alongside explicit sending,5/5/5 affected suites/private5 and both Java artefacts4receive cases/ref/direction. Verified2/claimed3 and storage release remain distinct from delivery. Real-router/lookup/streaming/backend/default activation remain open. [Review](evidence/20261029-i2cp-bounded-receiver/REVIEW.md). |'
p.write_text('\n'.join(lines)+'\n')
notes={
'migration/tasks/P07-02b-i2cp-initial-session.md':f'''\n\n## Bounded explicit datagram receiving\n\nSource `{SOURCE}` qualifies legacy Available→Begin6→Payload31→End7 and immutable initial fastReceive directPayload/noEnd. Queue4/160KiB, one legacy fetch, control4/recent64, unread-ID protection beyond recent rotation, signed2/current recipient versus unsigned claimed3, corruption drops and queued reentry/delete/close fences are covered. End releases router storage, not application acceptance; finite recent history is not lifetime replay protection. Fresh5/5/5 and private5 passed, with four actual receive/control Java cases per reference/direction. R1 Clang fixture format warning was fixed by one newline; all raw attempts remain separate. [Review](../evidence/20261029-i2cp-bounded-receiver/REVIEW.md). Actual stalled-socket overflow, real router/lookup/Streaming/backend/public activation remain open.\n''',
'MIGRATION_PLAN.md':f'''\n\n## 2026-10-29 bounded explicit I2CP datagram receiver\n\nQualified source `{SOURCE}`: fresh5/5/5 affected suites, private5, actual both-JAR receive/control4cases/reference/direction. One dist promoted, predecessor/testcopies deleted; both proposed registries passed first. Scope remains explicit bounded legacy/fast2/3 datagrams, not real-router/backend/Streaming or delivery parity. [Evidence]({E}/verification.json). Continue profile-I/O executor and explicit I2P UDP tracker work, then the rest of the full plan.\n'''}
for name,text in notes.items():
 with (R/name).open('a') as stream:stream.write(text)
p=R/'migration/ACTIVE.md';s=p.read_text();start=s.index('## Current execution');end=s.index('## Latest qualified batch',start)
s=s[:start]+f'''## Latest qualified batch / next work

L=R/build/i2cp-receive-followup-20261029. Qualified source `{SOURCE}`, implementation61d1, exact9paths. L/root-adoption-r2.json SHA09cdfdec3a3f74bba188584b4aaa6538f023d22c06cc96cc7fe6c35199359a9a. Fresh r2 GCC26888/Clang24496/SAN14884 ALLJOINED0/PASS5/5/5;8/5/5 products. Private5+bothJava receive4/ref/direction PASS. L/finish-r2.py executed ONCE successfully, NEVERrerun. Current dist `{promotion['manifest_sha256']}`,119leaves, previousbcbcb9+test5/QtTestdeleted. BOTH proposedregistriespassed beforepromotion, byteidenticalR/Nlocks. Evidence R/N `{E}`. Sourceonlynewlinecorrection7c33, priorr1 GCC/SAN5PASS andClangcompilewarning retained withoutfinalcredit; no assertions/product/deadlineschanged. Metadata commit/push next. Next original checkpoint expected-parentec450861eb4857f78c53d60b707d4fa9b6a0b25b. No questions.

Continue Driver approved profile-I/O executor and Oracle approved correlated lookup/scoped RAW18 I2P tracker slice; source-only capsules pending, no agentN/build/run. Codec independentreview available afterfreeze. Do not replay unrelated provider/oldJava or wholecompilerlogs. Max3jobs/nice5, one dist, running original Java/profile protected.

'''+s[end:].replace('## Latest qualified batch','## Previous qualified batch',1);p.write_text(s)
for name in ['migration/STATE.md','migration/FEATURE_MATRIX.md','migration/ACTIVE.md',*notes]:shutil.copyfile(R/name,N/name)
for root in (R,N):
 out=root/E;v=json.loads((out/'verification.json').read_text())
 for name in ('complete-docs.py','finish-source-review.json','finish-r2.log'):
  shutil.copyfile(H/name,out/name);v['artifacts_sha256'][name]=hashlib.sha256((out/name).read_bytes()).hexdigest()
 (out/'verification.json').write_text(json.dumps(v,indent=2)+'\n')
print('DOCS_RECORDED',SOURCE,promotion['manifest_sha256'])
