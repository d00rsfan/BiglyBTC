# P03-09 — Original storage/resume compatibility (CF-01)

State: active 2026-10-25. User authorizes all remaining plan work; this is the
first dependency chapter, followed by CF-02 and subsequent applicable chapters.
Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f;
native start b44ec22ebb3dbb6cee3ade23c32a7ad26a373926. Native main only.

- [ ] Integrate authentic compact/reordered/reordered-compact physical/control
  mappings, bounded reads/writes, missing data and original control persistence.
- [ ] Route actual same-session engine disk IO through original mappings;
  preserve ownership, counters, retirement and original resume semantics.
- [ ] Support original file links/incomplete paths and related keep/delete/move/
  relink within authenticated owned paths, without touching unrelated leaves.
- [ ] Execute generated partial/complete Java/native/Java handoffs plus failure,
  restart and mixed-boundary cases. Reuse retained original classes/helpers.
- [ ] Batch GCC/Clang/sanitizer checks, affected-only fixes, hosted and private
  portable qualification; publish main milestones and preserve complete rollback.
- [ ] Record full upstream/native hashes, actual receipts, residual work and
  next dependencies. Continue other authorized plan chapters after qualification.

Durable staging: build/storage-20261025/. Reuse three existing agents for
independent codec/IO, original oracle and file-operation integration. Root owns
shared Host/engine integration, CMake, source adoption, all actual builds and
tracking. Max3 compile jobs total; central batched builds, logs on disk.
No real original process/profile/payload access. No original source rebuild,
no system installation, no new native working branch. Prefer qualified existing
caches/dependencies over new clones and isolated build trees.

## Integration checkpoint (active, not qualified)

- Adopted20 source paths into native main working tree at baseline
  b44ec22ebb3dbb6cee3ade23c32a7ad26a373926; no commit yet.
- First coherent GCC build running; complete diagnostics retained in
  build/storage-20261025/gcc-r1.log. Root keeps source unchanged during build;
  mechanical fixes, resource bounds and final disk/control/recovery test sources
  are staged for one affected follow-up batch. No tests claimed yet.
- Thin Java fixture helpers compile under Java17/--release8 (r2); original5432
  compiled classes reused, original core not rebuilt.
- New controls authenticate profile/active/hash directory and fixed candidate
  identities, support valid backup repair and validate unchanged maps on close.
- Mapped disk IO stays in the same engine session; missing ranges never count as
  verified zeros. Selection C→L and X→R retains original formats; append-only
  reordered slots avoid optional destructive swaps.
- Agent stages: codec-io, fileops, oracle, disk-tests, original-control-hardening.
  Root engine staging + manifest/adoption/amend recipes are durable under build/.
- Next CF02 source audit already identifies selected hybrid identity and original
  UTF16 file-order projection gaps. Audit only, no CF02 production adoption.

### Runtime checkpoint

Actual original physical-format qualification passes:7 inherited read maps and3
bidirectional FMFile mutation cases, native codec binaryc20e7926b98a783f09821e636f6f8ad03d9fe6a657c2d0f90d0846a6d942020d.
Durable receipt/profile copy: build/storage-20261025/oracle-codec-r2/.
Original core was not rebuilt; all5432 retained reference classes/resources match.
This is format/API credit, not managed-owner/profile reload credit.

GCC r3 compilation passed. First functional run: original physical IO and
metadata routing pass; managed preparation hit the absolute-parent traversal
loop (staged correction), three new fixture errors corrected without weakening
production bounds. GCC r4 builds these corrections and a pre-effect retained
payload ownership guard; all new claims are bounded and indexed before writes.
Remaining affected runtime/original owner/compiler/private/hosted gates pending.

### 2026-10-25 retained runtime evidence and active correction

GCC r4 compilation and all six focused native suites pass (`gcc-functional-r2`,
7.1 seconds). Actual original physical codec matrix passes (`oracle-codec-r2`).
The real manager handoff (`oracle-managed-r3`) proves compact partial/complete
and reordered partial reloads. Reordered complete reload currently fails the
original controller's raw physical-size guard: 111616 physical vs103304 logical
bytes, despite a valid arbitrary piece map. Completion normalization is active;
this is a real compatibility defect, not a test recipe waiver. Reordered-compact
remains unexecuted after that failure. No whole managed-handoff credit yet.

Prepared coherent remainder batch: original128/256MiB geometry with64KiB compact
expansion scratch; exact signed/negative `filelinks2` projection; legacy save-root
and configured/missing storage-type resolution. These are source-only until
adoption and qualification. Existing GCC/owner/original receipts remain immutable.

### Coherent legacy/layout source batch

`amend-legacy-r5b` adopts24 exact-preimage paths, including mirrored SDK geometry
and link parser sources. GCCr5 compiles all affected targets in118.4s. Eight of
nine suites pass in the first run; the sole failure is an existing test's Cmarker
with full Lbytes. `amend-cas-fixture-r6` changes that fixture to source-defined
short R/.2 without changing assertions; its affected-only rerun passes0.1s.
Thus all9focused suites pass across retained r3/r6 receipts. The14-suite broader
engine/UI batch is now executing after a13.3s cache-reusing build.

### Actual same-profile handoff passes

After stopped reordered normalization, `oracle-managed-r8` passes all C/R/X
partial7/8 and complete8/8 Java/native/Java reloads, including real full-byte
reads. `defaults-all-r8` passes12 additional authentic root/default/filelink
handoffs. GCCr7 builds27targets and passes9focused+14broader suites. The initial
Xfixture expectation correction is pinned to actual cb8 DMChecker738–773; it
changes neither native transfer nor final original verification requirements.
Remaining release gates: pending authoritative redo file-operation guard,
secondary compiler/sanitizer, source/provenance, hosted and portable. Complete-
file normalization does not claim every oversized partial upstream case.

## Published core milestone

Source `c0b2fbb2071b7aae302d2db24ba3c824df874d0a` is on main. Final Clang23 and ASan/UBSan/leaks23,
GCC9focused plus14broader, hosted161+SDK2, and private12+13smoke pass.
Actual codec, C/R/Xpartial7/full8 and12legacy/default/link handoffs pass.
Portable dist promoted with complete rollback. [Review](../evidence/20261025-storage-resume/REVIEW.md).

This task remains active: next coherent batch covers active priorities/DND,
allocation-tail handling, stopped L→C and actual nonlinear Move handoffs. Other
compatibility families retain their checklist entries. No human question.
