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
