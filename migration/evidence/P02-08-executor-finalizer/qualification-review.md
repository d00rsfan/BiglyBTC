# Executor qualification independent static review

The revised runner and consumer pass static review with no blocking finding.
Runner SHA256: `2b3d096562f668ed4df81bc5565adfa5f63c78e2b9ca1de8fc8003ceb6a02eae`.
Frozen implementation manifest remains
`4c46f61dfdc117028551ee3db3886e562ebdc7178b69689d4acdfa3cbff8640c`.
No runner/CMake/compiler/JVM/native/test execution or main edit occurred. The
intentional final-location ROOT assertion was neither executed nor bypassed.

The original findings remain byte-for-byte in qualification-review-initial.md
and .json. The revised runner closes all three: exact executed tools and actual
Python interpreter are pinned; runtime snapshots are immutable per command and
retain the last good copy if a later copy fails; final guards cover all prior
CMakeCache/LastTest records, binaries, command logs and runtime copies. Progress
precedes each subprocess, copy state precedes each backup, and ordinary failure
records/logs persist. No sanitizer failure skip, suppression, retry substitution
or weakened semantic assertion is present. All four build/runtime roots must
be fresh. Work is nice5, at most two compiler jobs and serial CTest.

The original suite is byte identical and still uses unconditional require
checks in Release. Six new finalizer cases are each called once, alongside the
separate seventh startup rollback case. The runner requires three exact CTest
names, a complete passing CTest transcript and all three semantic pass markers.
ASan/UBSan/LSan and a separate TSan lane are required; runtime support is neither
assumed nor declared successful by this review.

Implementation review found no blocking defect. ShutdownCleanup unregisters
after all detached Work and stop-source copies. The worker predicate wait does
not use the cancelled token. Accepted futures/captures finish before hook
invocation, and invocation/capture release occur unlocked on the worker before
host join returns. Startup flags publish only after jthread construction;
rejection/rollback release the hook on the caller outside the lock. The pinned
headers and exact fragments confirm the prepared stop-state allocation failure
precedes OS thread creation. This is neither injected OS thread exhaustion nor
Java equality. Host exclusion of ordinary concurrent callers during destructor
entry and no Executor self-destruction remain explicit requirements.

The consumer traversal is correct after promotion. Its lack of PIC, unlike the
main core target, remains an optional code-generation consistency note rather
than an observed functional failure. Source/header/original HEAD-index guards
remain intact. All prior implementation/test/artifact pins checked unchanged.
Exact reviewed hashes and the preserved initial findings are in the JSON report.
