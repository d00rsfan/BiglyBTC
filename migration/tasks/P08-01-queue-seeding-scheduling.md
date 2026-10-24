# P08-01 — Queue, seeding rules and scheduling

State: qualified bounded preview; full original provider parity remains in CF-04. User authorized this chapter on 2026-10-24 after Tools →
Options. Native work stays on main. Preserve the running original client, its
profile and real payloads; all qualification uses generated isolated data.

Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native starting point: `8239f5b42c3f2f56126b7a382dc75a4bd8114607`.
Read [compatibility followups](COMPATIBILITY-FOLLOWUPS.md) before closeout.

- [x] Inventory original queue/seed rank, first-priority, ignore, activity,
  startup, timed-rotation and user-override behavior against immutable source.
- [x] Implement deterministic value-only rules and compatible configuration
  controls; distinguish unavailable live observations from known zero values.
- [x] Make the persistent engine apply one queue authority without competing
  libtorrent auto-management; retain explicit stop/pause/force/order semantics.
- [x] Connect original SpeedScheduler rules to worker-owned schedule/ownership
  storage and engine rates/pause controls; retain user overrides and recovery.
- [x] Qualify deterministic interactions, actual engine behavior, original
  method/config interoperability, focused UI/Host regressions and compiler/
  sanitizer lanes. Reuse existing caches; at most three compile jobs total.
- [x] Commit/push milestones to main, update portable dist with rollback, and
  record immutable upstream/native hashes and remaining compatibility work.

Independent existing workers stage policy, scheduler storage and Java oracle
capsules under `build/queue-20261024/`. Root integrates shared files and owns
builds, global tracking, publication and promotion. Batch source edits before
tests and repeat only affected checks after failures. Keep full logs on disk.

No blocking user question. Record any later question in
`build/queue-20261024/QUESTIONS.md` and `migration/QUESTIONS.md`; continue work
that does not depend on the answer. This chapter does not start tags/history,
search/RSS, sharing/tracker, storage-layout or public-swarm chapters.

Qualification: [review](../evidence/20261024-queue-seeding-scheduling/REVIEW.md) and [receipts](../evidence/20261024-queue-seeding-scheduling/verification.json). No next chapter started.
