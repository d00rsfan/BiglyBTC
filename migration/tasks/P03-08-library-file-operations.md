# P03-08 — Library and file operations (CF-06)

State: qualified supported slice, 2026-10-24. Native main only.
Implementation dda0654715ffa38feea15784a92bf6f00502523e.
See [review](../evidence/20261024-library-file-operations/REVIEW.md) and explicit
remaining CF-06 families on the compatibility checklist.
Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f;
native start a965fd3cf0547e041d58512c56fea1cccf972ccb.

- [x] Audit original removal, relocation/relink and torrent creation behavior.
- [x] Add held-lease prepared/confirmed file operations with compatible saved
  state, safe payload scope, journal recovery and no unrelated-file deletion.
- [x] Certify engine retirement before filesystem changes, honor Scheduler/manual
  ownership and revalidate the approved plan after volatile checkpoints.
- [x] Add classic My Torrents removal/move/relink and File → Create Torrent UI,
  immutable worker packets, progress/cancellation and explicit delete confirmation.
- [x] Create ordinary v1 torrents from files/directories, tracker tiers/private/
  comment/piece size, no overwrite, and no hidden import/start side effect.
- [x] Qualify generated partial/complete data, interruption/failure and original
  Java/native handoffs; run one cohesive GCC/Clang/sanitizer/portable boundary.
- [x] Commit/push main milestones and promote dist with complete rollback;
  retain full upstream/native hashes, evidence and concrete remaining gaps.

Three existing workers stage storage, creator and UI independently under
build/fileops-20261024/. Root owns shared integration, builds and tracking.
Limit total compile jobs to3. Batch related changes, keep full logs on disk and
repeat only affected failing checks. Reuse previous caches/qualification recipes.
Never access the running original Java/profile/real payloads; all IO tests use
new generated owned directories. No system installation or DEB. Broader compact/
reordered layouts and v2/hybrid remain CF-01/CF-02 with explicit diagnostics;
this chapter must not silently claim their qualification or start another chapter.
No blocking user questions. Record any later questions in the chapter QUESTIONS.md.
