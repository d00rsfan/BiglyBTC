# Worker-owned final cleanup

Previously the executor resolved cancelled queued work on the shutdown caller.
A worker-return cleanup hook alone could therefore destroy affine engine state
while that caller was still destroying a queued capture using it. The revised
executor registers caller cleanup under the queue mutex and waits for all
registered cleanup to finish using a noncancellable condition predicate. Every
accepted active/queued future and capture completes before finalization.

The original bool start() symbol delegates to a new overload accepting a C++23
move-only noexcept Finalizer. The hook runs once and its captures are cleared
on the worker, outside the queue mutex. Stop callbacks and cancelled capture
cleanup remain unlocked and support reentrant/concurrent shutdown. Startup only
publishes started/accepting after jthread construction succeeds. Rejected/failed
hook captures release unlocked on the caller and failed startup may be retried.
Ordinary external callers must finish before host destruction; work/finalizers
may not destroy their executor. This preserves the existing ownership contract.

The legacy executor suite with all original assertions retained and seven new native cases pass three
named CTest entries in each fresh GCC16 Release, Clang24 Release, Clang24
ASan+UBSan+LSan Debug and Clang24 TSan Debug build. The six gated cases cover
immediate/rejected startup, full queue, normal/cancelled/failed resolution,
reentry, concurrent shutdown with blocked queued capture destruction, worker
shutdown and finalizer capture release before host join. The isolated seventh
case proves deterministic pre-thread stop-state allocation failure, uncommitted
state, unlocked capture cleanup and successful retry with one hook execution.
No sleeps fit a race window; explicit gates establish required ordering.

The first strict GCC build stopped before any CTest: inlining custom delete/free
into a standard-library promise constructor produced mismatched-new-delete.
Its full source/log/runtime/cache capsule remains under development/. A separately
reviewed revision moves the dedicated test allocation shim into its own source
file. Production and header remain exact; startup assertions
are equivalent after the observation accessor substitution. Clang then exposed
an aggregate assertion failure in the new finalizer suite. That suite allowed a
user stop callback to be unregistered on scope exit before the requesting thread
reached it. The independently reviewed next revision holds the registration
through an explicit callback-completed gate; no assertion is removed. The exact
failing boolean was not separately instrumented, so the observed aggregate
failure and the independently established test race remain distinguished.
The same lifetime hazard was proactively corrected in one legacy test with an
explicit promise gate. All original assertions remain, and the original source
is archived and guarded alongside the revised test. No legacy failure was
observed or invented. Both CMake targets include the extra allocation source.
V3 passed GCC/Clang/ASAN but TSan's strong new/delete runtime symbols conflicted
with the test's replacement allocator at link time, before TSan CTest ran.
That complete failure capsule is retained. The independently reviewed V4
test-only revision uses target-private GNU ld --wrap=_Znwm and forwards every
successful allocation to the real C++/sanitizer allocator. The TLS one-shot typed
failure, all assertions and production source remain exact. Static runtime
archives/linker hashes and actual wrapper symbols are additionally guarded.
No warning/optimization/sanitizer
suppression, test removal, timeout change or production workaround was used.
All four passing lanes have fresh v4 roots, distinct logs and immutable per-command
runtime copies. Nineteen source inputs, exact executed tool paths, startup
proof headers, actual link libraries, flags, binaries, caches, LastTest and
original Java HEAD/index are guarded. All assertions are native foundation
policy; zero original Java scheduler equality is claimed.

This provides final cleanup, not a graceful compatible Close result, reserved
command admission, deadline, successful storage flush/release or profile commit.
Those remain P03-05 requirements. The existing shutdown deque's allocation
behavior is not an out-of-memory guarantee. The application does not activate
a new engine by this change. No JVM, profile, torrent, socket or running original
process participates. The separately recorded P27 DB CLI sanitizer timeout is
still unresolved and does not block this independent qualified foundation.
