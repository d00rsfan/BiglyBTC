# P02-08 Worker-owned final cleanup

Status: implemented bounded worker finalizer; all four fresh compiler/sanitizer lanes pass three CTest entries.
Evidence: migration/evidence/P02-08-executor-finalizer/{matrix.json,review.md}.
Extends `runtime.executor` / F28.lifecycle / F30.bounded_executor for the
persistent-session work in P03-05. The original bounded executor remains
implemented; this task does not claim Java scheduler or plugin-host equivalence.

Preserve `bool start()` and add an optional C++23 move-only noexcept finalizer.
Accepted hooks run once on the worker after active/queued accepted operations
have resolved their futures and released their captures. Caller-owned cancelled
queue cleanup must finish before the worker finalizes. A stopped-token wait is
insufficient for that barrier. Stop callbacks, operation capture destruction and
finalizer invocation/cleanup remain outside the queue mutex. Concurrent and
reentrant shutdown calls must not wait on one another. Rejected/failed startup
must release hook captures unlocked on the caller and must not commit started
state; a later retry may succeed.

The host must exclude ordinary external callers before destroying the executor.
It joins the worker before releasing owned state. Work/finalizers must not
destroy their executor. The finalizer and its captures may reenter stopped
queries/shutdown, with no new work admitted. A cleanup hook provides no graceful
Close result, persistence/flush proof, deadline or reserved command admission.
Those remain separate persistent-engine requirements. Existing cancellation
deque allocation behavior is retained; no out-of-memory guarantee is invented.

Frozen draft: `build/recovery/active-drafts/executor-finalizer/`.
Original executor test SHA256:
`41b8ac27f40d257a199e03d48865cd32a7a8f91b5a655a7f94a2a1f7c5ac9ea4`.
Independent source/patch/API/lifetime review found no blockers before execution.
Seven new native cases comprise six gated lifecycle/concurrency cases and a
separate deterministic startup-allocation rollback case. The latter pins actual
qualified libstdc++ constructor/member order and injects a pre-thread stop-state
allocation failure; it does not claim to simulate every OS thread failure.

Integrate only the four implementation/test files, retaining the original test
bytes. Register the two additional tests and a standalone qualification consumer.
Run three named CTest entries on fresh GCC16 Release, Clang24 Release, Clang24
ASan+UBSan+LSan Debug and Clang24 TSan Debug builds. Preserve first failures,
logs and exact source/header/binary hashes. Leak/race checks stay enabled without
suppression. Verify the actual committed blobs before recording a native hash.
No profile, torrent, JVM, socket, original process or public network is involved.

First strict GCC build failed before CTest because inlining the custom allocation
replacement produced a mismatched-new-delete error in a promise constructor.
Its complete source/log/runtime/cache capsule is retained under
P02-08-executor-finalizer/development/before-startup-allocation-split. The
reviewed revision separates the dedicated test allocator into its own translation
unit. Production/header/original/finalizer tests remain byte-identical; startup
assertions, strict warnings, timeouts and sanitizer checks remain unchanged.
All four revised compiler lanes use fresh v2 roots and immutable per-command
copies; see qualification-native-inputs.json and matrix-v2-in-progress.json.

V2 then passed GCC but Clang's new finalizer test failed an aggregate assertion.
The complete V2 source/binary/log/Testing/runtime evidence is retained under
development/before-callback-lifetime-gate. A legal callback unregister race was
found in that test; the observed particular failing boolean was not separately
instrumented. V3 adds an explicit signal keeping the user stop_callback alive
until it completes. Production, old tests and all assertions remain unchanged;
the new gate preserves actual reentry coverage rather than dropping it. Four
fresh V3 roots retain strict warnings/deadlines and leak/race checks.

A follow-up source review found the same registration-lifetime hazard in legacy
shutdownAndReentrantStop. Before any V3 execution, root accepted one focused
legacy test change: an explicit callback-completed promise gate. Every original
assertion remains, and an additional wait assertion proves registration lifetime.
The original41b... source stays archived and independently hashed; the current
legacy suite's byte change is explicit, not hidden as an unchanged-file claim.
No legacy failure was observed. V3 now guards18 inputs, including both original
archived and revised legacy sources. See development/legacy-callback-lifetime-gate.

V3 passed all three CTest entries on GCC, Clang and ASan+UBSan+LSan.
The TSan build failed linking the startup-allocation test: its replacement
operator-new/delete symbols conflict with strong symbols in the TSan runtime.
No TSan test ran, and full qualification remains incomplete. Exact V3 sources,
logs, Testing files, cache/build rules and binary/runtime fingerprints are
preserved in development/before-sanitizer-allocator-wrap. A test-only allocator
interposition revision is under independent review; no race checks are disabled.

The independently reviewed V4 test-only revision uses target-private GNU ld
--wrap=_Znwm. Its one-shot typed failure branch is unchanged; successful
allocations forward to the real C++/sanitizer allocator. All test assertions,
production code, strict flags and deadlines remain exact. Both test CMake
consumers apply the wrapper only to startup rollback. V4 additionally guards
the actual static sanitizer runtime archives/linker and checks final wrapper
symbols. Nineteen source inputs are qualified; no V4 execution is claimed yet.

Native implementation: `d4d539e761e136c9916a9305c2f8fff48e5061f8`; all19 qualified inputs match actual committed Git blobs.
