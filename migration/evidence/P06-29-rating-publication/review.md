# Bounded rating publication policy

The original rating updater's queue/dispatch, completion/timer and removal
policy now has an owned C++23 implementation. The complete original selected
methods and nested callback bodies are pinned to azrating
1629340340d0bc391d9e582f9ee989a93d550581, with core context
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The immutable846-row source corpus
predates native work. Exactly653 successful scalar observations are compared;
193 boundary/callback observations retain zero native equality credit.

The private move-only serialized owner copies bounded clock/RNG tapes and labels.
Production transitions emit ordered receipts. Weak generation-bound download,
completion and timer handles have no callback cycle or raw owner backpointer.
Completion/timer delivery is once-only; stale old completion cannot clear a
later write. Reserved capacity preserves control delivery when ordinary queues
are full. Native close invalidates handles without callbacks and remains
available at capacity, separate from the modeled source destroyed flag.
Source duplicate removal, RNG insertion excluding the tail, every-add dispatch,
backlog-before-inflight checks, inline completion, wrapped10000ms timer and
same-runnable redispatch are preserved. No global cooldown is introduced.

Independent native review preceded builds. Independent qualification review
verified the exact eight-file implementation,91 older pins,18 class hashes,
source projections and result parser. Root tightened Git/nice to absolute tool
paths and added the Git runtime hash before building a fresh final source helper.
Its full18 class hashes match the actual V1/V2 helper. No source JVM ran and no
fixture was recollected or refrozen. The prior invalid helper schedule and its
single-STEP correction remain preserved independently of native qualification.

Fresh GCC16 Release, Clang24 Release and Clang24 ASan+UBSan+LSan Debug each pass
exactly three CTest entries:653 semantic comparisons,386 native ownership/
capacity assertions and112 CLI observations across14 cases. Leak checks remain
enabled, UBSan halts on error, and no assertion, timeout, warning or sanitizer
was suppressed. All three lanes passed on their first native attempt. The1959
cross-toolchain matches repeat653 unique source observations.

The matrix guards100 source inputs, original91 pins, both18-class helper roots,
compiler/tool paths, cache flags, binaries, archives and resolved ELF libraries.
Immutable per-command capsules retain logs, Testing and hash-checked runtime
copies. Thirteen historical helper inputs live in ignored build storage; their
exact existing source-draft archives are mapped explicitly for later Git blob
verification. They are never misrepresented as committed build paths.

No real AsyncDispatcher, timer thread, DDB publication, attribute writes,
eligibility, plugin host, profile, torrent, network or application activation is
implemented by this slice. Original throwing/reentrant/repeated callback groups
are characterized but excluded from native equality. Full plugin/migration
verification, integration hashes and whole-client performance remain pending.
