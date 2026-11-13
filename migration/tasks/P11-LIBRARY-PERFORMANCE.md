# Large-library startup and sustained seeding

Status: authorised2026-11-13 by the user's latest instruction to continue the
following chapters autonomously. This supersedes the preceding merge STOP.
Native is running for user observation. Preserve that process/profile/runtime
image; prepare changes in the existing build cache. No new agents or full
backups, rollback images, payload reads by development tools, private commits
or unbounded temporary outputs. Compiler logs remain local; inspect summaries
and failures. Commit/push meaningful milestones on main.

1. Bounded userspace CPU sampling and admission/log/I/O counters from the
   already-running Native, without ptrace stops or profile/payload writes.
2. Remove repeated decoding and deep copies of unchanged config snapshots.
   Fresh file reads/byte checks, parser limits, recovery ordering, profile
   ownership and mutation CAS remain mandatory. Cache only immutable trees;
   mutation detaches, cache is bounded and per actual held-profile lineage.
3. Measure generated scale cases and exact parse/cache/memory counters; cover
   unchanged loads, edits, outside writes preserving mtime, recovery, limits,
   retained aliases and isolation. Run affected persistence/queue/pause controls.
4. Reduce further measured library-wide work in startup and steady polls;
   preserve original scheduling, disk-check limits, tracker activity and UI
   command responsiveness. Diagnostics report stage timing and real progress.
5. Record immutable source/evidence and provide the next candidate. Do not
   replace a running preview or request a restart until a reviewable candidate
   is ready. Compare the private pre-launch baseline only after normal Exit.

Next independent priorities: remaining existing-library compatibility (including
selected-v2 hybrid warm resume), advanced engine/lightweight seeding and missing
native UI/settings dependencies. Follow the migration dependency graph; defer
RSS/embedded tracker/I2P/IP-filter work where independent. Browser/player last.
Questions requiring user input go to migration/QUESTIONS.md; continue independent
work. Historical continuation.json and M-CURRENT-HANDOFF are not current tasks.

Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f: GlobalManagerImpl,
DownloadManagerStateImpl, RDResumeHandler, ConfigurationManager and original
queue/checking policies. Existing source-qualified SDK unchanged.

## Qualified bounded chapter (2026-11-13)

[Evidence](../evidence/20261113-library-performance/REVIEW.md): immutable plain/
gzip snapshots, strict batched mapping checks, bounded startup continuation and
engine-owner API pins passed affected persistence/control/transfer tests.
128-row admission fell from31227ms after caches to4305ms after continuation/API
retention; these are stage diagnostics, not a Java benchmark. A separate
512-torrent no-network/no-payload settled fixture consumed3.19% of one core over
10seconds. No speculative batch-status rewrite. Current preview remains running
its frozen image. Delivery/real-profile post-Exit comparison is pending.
Continue independently with selected-v2 hybrid warm resume; no stop requested.
