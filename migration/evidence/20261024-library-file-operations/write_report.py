from pathlib import Path
import json
R=Path('/home/yurii/Java/BiglyBT');W=Path(__file__).resolve().parent;D=R/'migration/evidence/20261024-library-file-operations'
v=json.loads((D/'verification.json').read_text());source=v['native_source'];base=v['original_baseline'];manifest=v['portable']['manifest_sha256'];initial=(W/'initial-source-commit.txt').read_text().strip()
review=f'''# Library and file operations — CF-06

Qualified supported slice, 2026-10-24. Native implementation
`{source}` is pushed directly to [main](https://github.com/d00rsfan/BiglyBTC/commit/{source}).
Original source baseline: `{base}`. Starting native baseline:
`{v['native_start']}`. Three existing workers authored independent storage,
creator and Qt source capsules; root integrated, executed checks and published.
Source-only reviews are distinguished from actual execution receipts.

Classic My Torrents now offers Remove Torrent, Remove and Delete Data, Move Data
and Set Data Location for one selected ordinary torrent. File → Create Torrent
(Ctrl+N) creates v1 file/directory metadata; optional import is explicit and adds
stopped. Preview reads without stopping the torrent. Confirmation binds all
presented paths/counts and the worker scope; stale targets or changed scope fail
before engine effects. Active retirement is certified before storage effects,
then a fresh exact execution plan tolerates legitimate counter checkpoints.
Startup/force/manual pauses and acknowledged Scheduler authority survive moves.

The durable intent orders destinations before metadata and source cleanup.
Recovery precedes owner admission, merges only affected original rows and stops
on conflicting target/scratch identities. Explicit deletion checks original
flag0x40 and removes only approved leaves. Metadata archives and unrelated files
remain. Relink changes no payload and requires recheck. All scope limits and
remaining original families are recorded in [scope](scope.md).

Executed evidence:

- GCC:19 initial affected suites and9 integration suites, covering20 unique suites,
  then2 affected suites after the modern pause-map compatibility fix.
  The grouped additional build first caught a test-driver std::quoted name
  collision; its generated pause_data key length was then corrected before the
  actual original handoff. That actual check found a production orphan-pause
  defect: modern dictionary records use hash; legacy records are byte strings.
  Both removal and sparse recovery now select the correct identity, preserving
  unrelated survivor ownership and opaque values. The test-driver/runner failures and the production ownership defect are retained separately.
- Clang and ASan/UBSan/leak checking:12 focused suites each, then2 affected
  suites per compiler after the modern pause-map fix, against final sources. The existing private-LT counter translation unit retains its
  vptr-only exception; the provider is not claimed fully instrumented.
- Original Java17:3 native-created torrent decoder cases and4 actual
  GlobalManager file-operation reload/READY cases. Unicode/empty files, actual
  piece observations, mapped paths/content, saved intent/unknown values and
  removed-target absence with survivor preservation are checked. The Relink
  runner identity correction compares its previewed destination inode; source
  contents and post-Java physical maps remain checked. Original5432-class
  retention is verified; no original core rebuild or real profile access.
- Standard GitHub-hosted desktop and SDK workflows pass for the exact source.
  Actual counts: {json.dumps(v['hosted_counts'],ensure_ascii=False)}.
- Private bundled Qt6.12.0/approved LT portable run passes13 smoke rows and6
  chapter suites. Actual loader/RPATH/module checks, screenshots and logs are
  retained. Dist manifest `{manifest}`; the complete previous dist remains in
  the recorded rollback directory. No DEB/system installation was performed.

[Machine-readable verification](verification.json) contains receipts, full
hashes, commands and explicit remaining work. Backend copy coverage injects
EXDEV and checks real copied bytes; this does not claim a real cross-mount run.
Offscreen evidence does not establish compositor or public-swarm/performance
parity. Current non-Keep operations validate payload contents, so large moves,
relinks and deletes can incur substantial extra IO. CF-08 explicitly tracks
removing avoidable reads without weakening ownership/recovery guarantees. Original Java/profile/HEAD/index remain untouched. No human question is
pending, and no next chapter is started. Broad source families remain
in_progress in the registry; deferred paths do not advance integration hashes.
'''
(D/'REVIEW.md').write_text(review)
task=R/'migration/tasks/P03-08-library-file-operations.md';s=task.read_text();s=s.replace('State: active, user-authorized 2026-10-24. Native main only.','State: qualified supported slice, 2026-10-24. Native main only.\nImplementation '+source+'.\nSee [review](../evidence/20261024-library-file-operations/REVIEW.md) and explicit\nremaining CF-06 families on the compatibility checklist.');s=s.replace('- [ ]','- [x]');task.write_text(s)
p=R/'migration/STATE.md';s=p.read_text();start=s.index('## Active library/file operations');end=s.index('## Qualified queue',start);s=s[:start]+f'''## Qualified library/file operations (2026-10-24)

Source {source} is pushed directly to native main. Classic single-torrent
keep/delete/move/relink and ordinary v1 creation, certified engine retirement,
preserved pause/Scheduler authority and durable recovery pass20 unique GCC
suites,12 Clang and12 ASan/UBSan/leak suites,3 original parser and4 actual
original GlobalManager handoffs. Hosted desktop/SDK and private13 smoke rows/6
suites pass. Dist promoted with complete rollback; manifest {manifest}.
[Review](evidence/20261024-library-file-operations/REVIEW.md) and
[receipts](evidence/20261024-library-file-operations/verification.json).
Original-family limits remain explicit in CF-06 and the source registry.
No next chapter, active build/test/owner or human question. Preserve the running
original profile/process/ordinary HEAD/index. Do not rerun adoption/promotion.
Durable local work: build/fileops-20261024/; project: ../BiglyBTC on main.

'''+s[end:];p.write_text(s)
p=R/'MIGRATION_PLAN.md';s=p.read_text();old='''Active chapter: [Library/file operations — CF-06](migration/tasks/P03-08-library-file-operations.md).
User authorized implementation; preserve original profiles, batch changes/checks
and finish this chapter before selecting further work.
''';assert old in s;s=s.replace(old,'''Qualified chapter: [Library/file operations — CF-06](migration/tasks/P03-08-library-file-operations.md).
Classic keep/delete/move/relink, ordinary v1 creation, engine retirement and
original-compatible recovery pass original/native, compiler/sanitizer, hosted
and portable qualification. Broader original families remain explicitly in CF-06.
No next chapter started; no pending human question.
''',1);s=s.replace('Qualified chapter: [Queue, seeding and scheduling]','Previously qualified chapter: [Queue, seeding and scheduling]',1);p.write_text(s)
p=R/'migration/tasks/COMPATIBILITY-FOLLOWUPS.md';s=p.read_text();old='''- [ ] **CF-06 — Remaining library/file operations (P03/P04/P08).** Implement
  removal versus deletion, relocation, torrent creation and associated dialogs.
  Verify intended confirmations, original metadata consistency, recoverable
  failures and preservation of unrelated files using generated data.
''';assert old in s;s=s.replace(old,'''- [ ] **CF-06 — Remaining original library/file families (P03/P04/P08).**
  [P03-08](P03-08-library-file-operations.md) now qualifies single selected
  ordinary keep/delete/move/relink, v1 file/directory creation, compatible intent,
  retirement/recheck, confirmed recovery, actual original reload and dialogs.
  [Review](../evidence/20261024-library-file-operations/REVIEW.md).
  Still implement bulk operations, archive-deletion controls, advanced/virtual/
  multi-source and256MiB original creation options, wider repair UX, and
  supported linked/nonlinear/shared/merge/overwrite storage workflows.
  CF-01/CF-02 retain wider layouts and v2/hybrid qualification. Preserve explicit
  refusal for unsupported paths; the broad source families remain in_progress.
''');p.write_text(s)
p=R/'migration/tasks/COMPATIBILITY-FOLLOWUPS.md';s=p.read_text();s=s.replace('  measurements. Track the private counter shim', '  measurements. Profile/optimize the current full-payload identity-validation\n  reads in move/relink/delete, including same-filesystem hardlink moves, without\n  weakening confirmed ownership or crash recovery. Track the private counter shim');p.write_text(s)
p=R/'migration/upstreams.lock.json';lock=json.loads(p.read_text());ids={'library.file_operations','formats.torrent_creation','ui.classic_file_operations','plugins.speed_scheduler_runtime'}
for c in lock['components']:
 if c['id'] in ids:
  commits=[initial,source] if c['id']=='library.file_operations' else [initial]
  for commit in commits:
   assert not any(x['commit']==commit for x in c['native_commits'])
   c['native_commits'].append({'repository':'https://github.com/d00rsfan/BiglyBTC.git','commit':commit,'purpose':'implementation'})
  for key,value in [('review_records','migration/updates/library.file_operations/20261024.md'),('evidence','migration/evidence/20261024-library-file-operations/verification.json')]:
   if value not in c[key]:c[key].append(value)
  # Broad original source families retain existing integration baselines.
  if c['id']!='plugins.speed_scheduler_runtime':assert c['state']=='in_progress' and all(x['integrated_through_commit'] is None for x in c['source_coverage'])
p.write_text(json.dumps(lock,indent=2)+'\n')
p=R/'migration/updates/library.file_operations/20261024.md';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(f'''# Initial bounded library/file-operation port review

Original repository https://github.com/BiglySoftware/BiglyBT.git at {base};
reviewed baseline remains {base}. Native initial implementation {initial}, compatibility correction {source} at
https://github.com/d00rsfan/BiglyBTC.git. No upstream revision advancement.

Scope and evidence: [chapter review](../../evidence/20261024-library-file-operations/REVIEW.md),
[remaining paths](../../evidence/20261024-library-file-operations/scope.md),
[execution receipts](../../evidence/20261024-library-file-operations/verification.json).
Original GlobalManager/DownloadManager removal and move/save-directory paths,
TorrentUtil/NewTorrentWizard dialogs and TOTorrent creators inform this slice.
Java/layout/persistence compatibility uses the existing immutable source lock;
creation delegates generation to exact LT2.0.12 at
740a0b9aeabe00e762cc0efe4a0f27593db2550b. Existing Qt/provider locks remain.
Scheduler acknowledged pause transfer extends the existing runtime component;
its previous upstream review/integration hashes remain unchanged.

Ordinary confirmed operations and v1 creation are implemented and exercised.
Bulk, advanced/virtual/v2/hybrid creation, nonlinear/linked/shared mappings,
merge/overwrite moves, archive deletion and wider repair UX remain explicit.
Consequently broad library/creation/UI source coverage stays in_progress and
its first_ported/integrated-through hashes remain null. This prevents partial
happy-path evidence from claiming whole-original-family integration.

Three source authors supplied independent frozen capsules; root performed
integration/runtime qualification. Source-only reviews and actual commands are
retained separately; no unavailable external reviewer or whole-client parity
is implied. No real original user profile/process was used.
''')
print('Chapter review, task, plan/state, follow-ups and partial-family provenance updated')
