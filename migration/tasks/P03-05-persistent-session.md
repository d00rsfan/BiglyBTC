# P03-05 Persistent owned local session and seeding

Status: in progress. Original export/pause and completion references are frozen.
A bounded complete-file seed owner V2 has passed independent source review;
its34 generated cases remain unexecuted. The qualification runner needs owned
process-group quiescence and SIGTERM cleanup corrections before three fresh
lanes. The normal application engine remains unchanged. No source semantic
equality, persistent application activation or performance result is claimed.
Source baseline selected before this work:
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; qualified libtorrent source remains
`740a0b9aeabe00e762cc0efe4a0f27593db2550b`.

Replace the development runner's per-operation session with one reusable engine
owner. Keep it alive after verification to upload to freshly owned loopback
peers, while queries and explicit pause/resume/stop/close remain responsive.
Borrow the Host's one profile lease. Preserve original file paths, payload
inodes, settings, unknown fields, stopped/paused rows and Java restart intent.
Normal close must not silently become manual Stop.

Phase 1 independently froze six complete original GlobalManager methods:
exportDownloadStateToMapSupport, pauseDownload, resumeDownload, resumingDownload,
stopPausedDownload and isPaused, with exact PauseState/HashWrapper fragments.
The corpus contains 14,068 observations: 2,353 export, 10,657 pause, 198 callback
order, 634 getter-fault, 95 alias and 131 provider-boundary. There are 67 expected
source exceptions, all in getter/provider groups. Seventeen fresh helper classes
and all source/plan/fence hashes are pinned. No native equality allocation is
made at this source-only stage. See the phase1 source-freeze verification and
immutable fixtures under `migration/fixtures/session-export-pause/`.

Supplied getter/stop/force/save/tag boundaries record intentions. No ordinary
manager/controller/disk constructors execute. The export returns a fresh map;
unknown-field preservation must continue through the existing structured-edit
writer. Source Long-reference compression, repeated getters, callback ordering
and mutable aliases remain characterized separately from native ownership.
Default declarations are pinned source facts, not execution of config providers.

Remaining implementation gates:

1. Preserve the completed source freezes:14,068 export/pause observations and
   2,280 completion observations,17+22 actual helper classes. All16,348 receive
   zero native equality credit from the current owner. Source intentions do not
   execute real trackers, moves or ordinary controllers.
2. Qualify the reviewed seedV2 in fresh GCC16, Clang24 and leak-enabled
   ASan/UBSan lanes:34 owner cases plus25 selected retained CTest rows. Preserve
   first failures. Compile the production translation unit without the private
   test macro. Tests use only generated complete final-path single-file batches
   and exact fresh numeric loopback peers, with no downloads or discovery.
3. Implement and qualify the separately reviewed Qt-free Host contract: bounded
   admission/result tickets, cancellation-safe Open attempts, stable entry
   generations, fresh worker-time polling and truthful terminal Close. The
   qualified Executor finalizer supplies caller-cleanup ordering before worker
   finalization; it is not the Host. Existing shutdown allocation can fail, so
   no general noexcept/OOM guarantee is claimed. Integrate app/GUI only after
   Host qualification, within the user's existing implementation authority.
4. Prove per-torrent storage release before completion renames. Normal and
   checking-pause branches differ; a generic pause/cache/remove/save-resume
   alert is not unconditional proof of closed files. Stale/dropped alerts,
   explicit cache-release callbacks, deadlines and failure paths need proof.
   Retire old handles, preserve the existing completion journal and readd final
   paths with a new runtime generation. Never set seed_mode without validation.
   Whole-session join plus independent file identity/fsync is the current
   complete-file Close boundary; automatic completion promotion stays blocked.
5. Decide the bounded partial-checkpoint seam. RDResume.saveResumeData remains
   separate work. Conservative original recheck flags are permitted as an
   explicit first development behavior; no fabricated partial-block equality or
   performance improvement follows. Relevant unsupported policy combinations
   must fail before new writes rather than changing their settings.
6. Qualify actual continued upload to two fresh leechers after download returns,
   a responsive second torrent, one lease, pause/resume/manual Stop/normal Close,
   nonduplicated counters and generated Java/native/Java handoffs. Retain prior
   single/multifile v1/v2/hybrid regression and crash evidence. Repeat GCC16,
   Clang24 and leak-enabled sanitizers on the actual integrated source hashes.

No live profile, running Java process, public swarm, DNS, tracker, DHT, router,
Tor/I2P or plugin runtime is authorized by these development tests. Broader
storage/queue/network/UI behavior remains required by the full migration plan.
Source helpers and audits are persistent under
`build/recovery/active-drafts/persistent-session-audit/`; source/recovery evidence
must survive interruption before any native completion claim.


The main source tools now live at `migration/tools/session_export_pause_{oracle,fixtures}.py`
and `migration/tools/session_completion_{oracle,fixtures}.py`; the Java template
filenames are unchanged under `migration/fixtures/java/`. The historical source
plans/previews/inspection records remain byte-identical evidence. Phase2 plans
221 scenarios and 2,280 commands, pins four complete original bodies and 22 raw
   original files. Its first root-run capture now has22 actual helper classes
   and2,280 frozen source outputs. Its reviewed
scope distinguishes remaining excluding DND from full remaining, records source
wall/monotonic order, already-complete and force-retention paths, caught save
failures and callback intentions. Real controller/disk/flush/publication behavior
remains outside this source seam.

Defaults inspect/validate without rewriting historical metadata. Both explicit
builders verify exact compile/runtime argv and persist complete preparation and
failure state under `build/recovery/generated-references/`. Every relocated
phase1 build must reproduce the existing 17 actual frozen class hashes. The
phase2 first actual class map can only be recorded after a root-granted fresh
build. Existing phase1 fixtures and all earlier classes remain immutable. See
[source tool commands and scope](../evidence/P03-05-persistent-session/source-tools-README.md)
and the [phase2 independent review](../evidence/P03-05-persistent-session/phase2-source-draft/independent-review.md).
No compiler/JVM/native operation was run for this relocation; no native hashes
or equality allocation advance.

The following notes retain earlier work states; the status and implementation
gates above describe current work. Frozen evidence remains authoritative for
actual source/binary identities and outcomes.

Storage-barrier follow-up: six exact libtorrent files and source inspection are
retained under evidence/P03-05-persistent-session/storage-barrier-followup. The
checking pause branch and indistinguishable old cache callbacks prohibit treating
the next generic alert as a rename barrier. A specifically pinned mmap retirement
fence may provide stronger proof; independent protocol review is in progress.
No safe native barrier is claimed by this inspection alone.

Root source execution completed: relocated export/pause reproduced all17 actual
classes byte-for-byte, and first completion source capture froze all2,280 rows
against22 actual helper classes. Strict predeclared error contracts passed,
including46 outward supplied faults. The actual88-file completion helper work
is persistent; no fabricated precompiled phase2 baseline is used. Native owner
implementation, safe completion promotion and native equality remain separate.

Current reviewed owner source is durably captured in
`migration/recovery/20261018/persistent-seed-v2/manifest.json`; its34 cases remain
unexecuted. V1 and its three review findings are preserved. V2 fixes separate
entry/byte-cap tests, expired operation handling before queued success, and
current named parent identity checks before Close fsync. This development owner
borrows one lease and owns one session for one or two complete final-path
single-file selections. Runtime pause/resume/stop do not export original Java
pause/counter/Stop state. Full partial/multifile persistence remains required.

Runner review requires both its command leader reaped and exact owned process
group gone before stable evidence copies. Nonquiescent copies must be marked
partial failure evidence; SIGTERM must unwind owned cleanup. No global process
scan or existing process probe is permitted. Versioned fixes keep original
tests, deadlines and frozen runner inputs. After all lanes pass, seven reviewed
engine files may be adopted with an explicit CMake wiring review and actual
Git-blob provenance; ordinary application activation remains a separate gate.

Checkpoint refusal diagnostics qualified at `0fec0cca4a0f58487eb86cf9691e1e96641e1ee0` / `f742f6201c8d91cab5c96877d99acb835dc7e8b2`:original file/snapshot guards remain unchanged;actual named-inode replacement and oversize tests identify torrent/file/cause and preserve progress/inventory/payload.62 managed-storage families/4825 assertions and related owner/profile/Host/payload checks pass. [Evidence](migration/evidence/20261118-checkpoint-conflict-diagnostics/REVIEW.md). Reported real-library failure cause remains unknown;current requested daily unchanged. Continue investigation and whole plan.

Allocated missing-data preflight qualified at `43f419f86b777f3ff43e3e2dccbdfd7d58263f6c` / `14ec4a9b2f7e64c73755ebf727d7e83f6d0fb412`:a real generated-profile baseline reproduced replacement/staged-resume creation for missing allocated1 data. Download now uses original low32 Long/intValue semantics and refuses missing real noncompact files before any state/path mutation,with present/unallocated/malformed/wide controls.64 storage families/5327 assertions plus current profile/tag owner/Host/payload pass. [Evidence](migration/evidence/20261118-allocated-missing-preflight/REVIEW.md). Compact boundary policy and actual reported final-modal cause remain open;daily/payloads unchanged. Continue whole plan.

Compact-file requirement value protocol qualified at `a0f2bad9d8ac8681155a9c55cb3c85be51406f6a` / `5fd90b6892df11afc3481b2159b5530743f98658`:23,100 actual pinned RDResumeHandler calls and actual TOTorrentFile geometry match Native exactly;zero-origin/aligned-zero ordering controls,explicit invalid/unknown errors,CTest and instrumented Core/driver ASan+UBSan+LSan pass.384 naive-boundary counterexamples are retained in evidence. [Evidence](migration/evidence/20261118-compact-file-requirement/REVIEW.md). Pure prerequisite only;effective skipped/default/padding/writer ownership and real Compact missing-data admission remain next. No daily/profile/payload changes;continue whole plan.

Original priority restore/cleanup plan qualified at `bece99d00579dd9b646cac9f1c08910beab48b1e` / `a61b1bfec3ddb10c11f3b114649d671f8985cce1`:1,678 actual pinned DiskManagerUtil calls match keeping constructor values versus complete replacement and cache-only cleanup,including Long narrowing/sparse/caught slots. Native23,575 assertions,raw-policy/new CTest,Clang24 and standalone ASan/UBSan/LSan pass. [Evidence](migration/evidence/20261118-original-priority-restore/REVIEW.md). No engine/writer activation;complete current census/default/padding and unskip/conversion ownership remain next. Current daily unchanged;continue whole plan.

Required Compact missing-data preflight qualified at `5037417fa0b8a350b47b227cbd040427994939b8` / `5d84957dd9ff2dd5466c38cc217554d25dfcc85d`:actual baseline creation/resume mutation reproduced,then C/X selected/shared-boundary missing data refuses before writes with preservation/optional/present/new controls.4,320 actual constructor/receiver/predicate rows match Native236,001 assertions;100k-file batch,Core3CTest,managed67families5,979assertions,normal owner/Host/payload,Clang and standalone ASan/UBSan/LSan pass. [Evidence](migration/evidence/20261119-required-compact-preflight/REVIEW.md). Explicit requested-kind getter supplied;no Core/global-default getter qualification. Current daily immutable;continue current skip/unskip/conversion ownership and whole plan without a pause.

Original skip conversion intention plan qualified at `722769e60464920e4ba79513749b9bdac68b834c` / `e462eddb7266be6a214335afb14f618a9ab0e43c`:actual public setters and real CacheFile conversions provide1,872rows/7,200constructors/490groups/644cachecalls,including216partial-failure prefixes. Native request order/masks and receipt/flag gates match over40,876assertions;CTest,Clang and standalone ASan/UBSan/LSan pass. [Evidence](migration/evidence/20261119-unskip-storage-intentions/REVIEW.md). DND and later existence owner are explicitly supplied construction/destroyed seams;no Native effect/recovery activation. Continue actual intent/current-source/conversion journaling and whole plan;daily unchanged.

Explicit unchanged-policy unskip intent qualified at `c1ec03fd92b4b12c7b504456def282823df5cdd5` / `c380ae199ad2be1589f5486c9c268e9bf79ffd72`:Host retains selected indices;identity-layout C/X-to-L/R metadata survives plain/gzip crash cuts and semantic journal tampering is rejected. Actual Original cache4readbacks and Native18profileQt cases pass,alongside Host/legacy/managed and standalone codec Clang/sanitizer checks. [Evidence](migration/evidence/20261119-selection-storage-intent/REVIEW.md). Packed/control/DND/allocated-missing cases refuse;no physical conversion or raw-changed legacy fix claimed. Continue coherent effective policies/current-source snapshots and full conversion/allocation owner;daily unchanged.

Active C/X constructor policy and separate saved-source snapshots qualified at `daf8bbb6e420fcc2e37935df479964e3e694021f` / `7fe103c0e5d4a62a13b48bcd0e012eac39f13b99`. Fresh20actual stopped factories/20active constructors match20Native passive/owner/SDK rows,including deliberate stopped/active differences.68storage families6919assertions,108adapter assertions,actual C/X queue source proof,affected Host/queue/scrape/read-only and Clang/sanitizers pass. [Evidence](migration/evidence/20261119-effective-file-policy/REVIEW.md). Passive query unchanged;initial equal-role test corrected before publication. Identity-layout raw-changing unskip v2 is included to preserve restart behavior;full conversion/allocation/DND and active file-detail roles remain open;daily immutable
