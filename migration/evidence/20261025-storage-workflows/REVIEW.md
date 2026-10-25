# Active file selection, compact storage and nonlinear Move

Native implementation: `2b982ac35f8458f98b194cc4ac0d2ee9adbc36d5` (main).
Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Status: qualified and published. All local, actual Java, hosted and private
runtime gates pass. Dist is promoted with complete prior-dist rollback.

Active priority/Skip edits now retire the exact admission, persist original
policies/DND paths, then restore its manual queue/force/pause intent. Ordinary
Skip preserves data. The explicit Files action **Do not download and reclaim
space…** confirms one linear file and requires every profile handle already
retired. It stores only original compact boundary bytes through a bounded,
checksummed, recoverable journal; original progress is invalidated for recheck.

Reordered final-slot allocation preserves the actual old tail before map
publication. Stopped handoff also normalizes physically complete allocation
whose hashes failed, without certifying its bytes. EOF-short allocated ranges
stay missing and untouched. All effects retain existing ownership/control CAS.

Each of GCC16, Clang24 and ASan/UBSan/leaks passes15 affected suites:14 unchanged
suite results plus the corrected worker test. Source/binary maps and actual
receipts retain the exact commands. Earlier fixture failures remain visible:
one test reused a stale Target after intentional rejection; one Java comparison
incorrectly assumed READY leaves zero lifetime counters unchanged. The corrected
checks use a fresh actual Target and the actual original-compacted twin;
payload, native lifetime preservation, transfer and recovery assertions remain.

Actual original Java prepares linear8/8, compacts a reference twin and verifies
the native result as3/8 (the original intentionally skips compact interiors).
The same profile then passes native partial7/8 and full8/8 with Java reloads
and complete logical-byte reads. Three C/R/X cases additionally pass6 real
Moves, native partial/complete transfers and Java READY reloads. Original core
classes are retained unchanged; only small fixture helpers compile on Java17.

The portable candidate passes4 deployment-sensitive suites under its private
Qt6.12/source-qualified provider,13 smoke rows and28 captures. Pure/static tests
are covered by compiler lanes rather than repeated under packaging. Shipping
provider remains the existing c35f artifact; the future v2/provider work is not
part of this implementation. The existing private-counter vptr-only exception
remains explicit; this does not claim a fully sanitized provider DSO.

CF-01 remains open for the unsupported EOF-short/other layout corners; CF-02
v2/hybrid, shared/merge/overwrite/bulk families and broader providers keep their
own open tasks. A pending journal requires native recovery before switching to
Java; an unpublished .saving is refused for explicit review. There is no atomic
power-loss handoff claim. The stricter all-profile retirement requirement is a
bounded native workflow, not full original controller parity. No running
original process/profile or real payload was accessed; no system install.
