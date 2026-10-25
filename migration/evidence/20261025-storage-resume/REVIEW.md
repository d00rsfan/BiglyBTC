# Original mapped storage and generated-profile handoffs

Status: supported core milestone qualified and published. The full CF-01 family
remains in progress for the explicit follow-ups below.

Native implementation: `c0b2fbb2071b7aae302d2db24ba3c824df874d0a` (main).
GCC9focused plus14broader, final Clang23 and ASan/UBSan/leaks23 pass. Actual
C/R/X partial/complete and12legacy/default/link handoffs pass; final codec matrix
passes. Hosted desktop161 and SDK2 pass; private portable12+13smoke pass.
Dist is promoted with complete previous-dist rollback; no system installation.

Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native parent: `b44ec22ebb3dbb6cee3ade23c32a7ad26a373926`.
Source-qualified libtorrent baseline:
`740a0b9aeabe00e762cc0efe4a0f27593db2550b`.
Shipping provider SHA256:
`c35f1b0408508c019f002c438d70dd361a55be20286ded56711efd6bba2424e3`.

## Implemented scope

Original L/C/R/X physical layouts and fixed control leaves remain readable and
writable through authenticated descriptors in the same owned native session.
Missing ranges remain explicit. Controls retain unknown fields and fixed-name
identity/CAS checks, including valid backup recovery and unresolved-saving
refusal. Ordinary linear payloads retain the existing disk backend.

Compact selection expands C to L with a durable identity journal before original
policy publication/control retirement. X selection becomes R. Actually complete
oversized reordered files move only the short tail to the final physical slot,
using bounded redo before each in-place swap and syncing before completion.
Pending native redo blocks ordinary IO and file operations until recovery; a
valid cleared primary can supersede its stale pending backup.

Original legacy save roots, configured/missing storage policies, signed negative
filelinks2 and explicit owned external links are resolved without touching the
real profile. Cross-admission path/inode claims prevent conflicting managed
payload ownership. Existing completion suffix and DND prefix transitions retain
identity journals. Existing standard original128/256MiB geometry is admitted;
compact expansion scratch is bounded to64KiB and IO request limits remain.

## Executed evidence so far

- GCCr7/r9 coherent builds and all9focused tests pass; all14broader engine/UI
  tests pass onr7. Immutable receipts/logs are retained under
  `build/storage-20261025/` pending final evidence export.
- Actual Java/native/Java managed handoffs pass for C/R/X with7of8pieces after
  partial transfer and8of8 after completion, including final original file reads.
- Twelve additional actual original-class handoffs pass for legacy roots,
  absent/empty/default storage policy and negative external file links.
- Retained original compiled classes are reused; only fixture helpers compile.
  No original core rebuild, real profile/process access or system installation.

## Explicit remaining boundaries

The completion normalizer is currently authorized for fully verified files.
All-mapped full-extent failed-hash files and EOF-short reordered allocation need
further source/runtime qualification; the latter is not attributed to an
upstream defect without an actual original fixture. Active selection/DND and
stopped L→C policy changes are separate next work. General legacy charset and
shared/merge/overwrite workflows retain their explicit unsupported boundaries.

In-place completion redo is native recovery, not an atomic Java-visible swap.
Interrupted normalization needs native repair before handoff; original recheck
remains authoritative. The full CF-01 family and wider epic are not declared
complete by this successful generated-profile matrix. CF-02 full/selected hybrid
identity and swarm policy capsules are source-only and unadopted here.
