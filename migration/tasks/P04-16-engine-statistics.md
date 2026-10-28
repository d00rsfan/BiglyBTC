# P04-16 Measured transfer-engine session statistics

State: implemented — bounded engine-session statistics qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `e9eef6cbf633207ae4b44691147e5ece4f5b03be`.
Use the pinned libtorrent2.0.12 dependency/source, not current web documentation
as a substitute for the versioned API. Source audit and exact hashes belong in
`migration/evidence/20261018-statistics-engine/`.

Add actual owned-engine measurements to Tools > Statistics > Transfers > Current.
The experimental engine creates one session per operation; label this scope.
Do not represent it as application-wide global/session/history parity. Keep the
saved original BiglyBT.statistics reader and all profile formats unchanged.

Resolve four named libtorrent cumulative counters at runtime, requiring counter
types and safe distinct indices: net.recv_bytes, net.sent_bytes,
net.recv_payload_bytes and net.sent_payload_bytes. Combined values already include
BitTorrent payload/protocol. Protocol is checked combined-minus-payload; IP
overhead, tracker and DHT metrics remain separate and excluded. Copy observations
before the next pop_alerts. Integrate both single/multifile paths through the
existing shared alert consumer. Use one outstanding stats request and at most5Hz
periodic requests. No callback means no new stats requests or metric lookup.
Sampling failure/unsupported metrics cannot change completion or transfer state.

The Qt-free accumulator accepts nonnegative, consistent cumulative facts and
strictly increasing owned steady observation times. Never use the dependency's
potential wall-clock alert timestamp for elapsed rates. First observation has
known totals and unavailable rates. Subsequent rates use exact floor(delta*1e9/
elapsed-nanoseconds), with no overflow or inferred zeros. Invalid/regressing input
does not mutate the baseline; the sampler disables on discontinuity. A fresh
engine gets a fresh accumulator. No cumulative counters are added across runs.

Bind immutable GUI packets to held profile session, operation, sequence and
torrent target. Preparing clears old engine facts, Observed carries complete
actual samples, Ended retains last observed values without claiming an exhaustive
final accounting. Reject old operations/sequences, wrong sessions/targets,
post-terminal replies and counter/time regressions. Coalesce worker observations
without an unbounded GUI queue. Cache the latest owned packet in the Window so
opening/reopening Statistics during/after transfer restores real measurements.
Saved-total refreshes and Ctrl+1 preserve them. New profiles clear them. No page
can start a sampler, transfer or timer; no hidden/minimized animation is added.

Qualification: integer limits/rate arithmetic and discontinuities; runtime sampler
semantics; scoped Qt rendering/cache/stale/terminal/reentrant cases; real generated
single/multifile loopback transfers with file identity/content preservation and
no statistics writes; cancel/failure lifetime. Preserve the existing48-unit
regression set, current compiler/flags/deadlines, GCC16/Clang24 C++23 and Qt6.12.0.
Run leak-enabled sanitizers, actual three-scale preview smoke, standard hosted
CI, exact source/installed-RUNPATH/dist verification and retain the previous dist.
No original client/profile/payload or public swarm, discovery, tracker, DHT,
router or plugin runtime is touched. Persistent application ownership/history,
all other P03 gates and whole-client/performance parity remain unfinished.

Root owns engine integration, worker/window queues, real loopback tests, shared
CMake, plan/evidence and publication. Parallel agents own the pure accumulator,
private session sampler, and DTO/page/tests. Cross-review source after authoring.
Keep durable drafts/builds under build/statistics-engine-20261018, with temporary
generated profiles only under /tmp/biglybtc-*. No blocking user question.

<!-- p04-16-engine-statistics-completion -->
## Completion evidence

Source `5fc34b588713112b126f3cebdfc891020301a281`, metadata `30569e2631427003a4a92e7edee3572a3b34e740`,
annotated tag `milestone-engine-statistics-20261018`. All 28 changed/new source files match tested Git blobs.
GCC 50 unit suites, Clang 3 focused suites and 9 leak-enabled ASan/UBSan suites
pass without skips. New arithmetic: 22 cases, 3,719 assertions and 512 independent
rational vectors; Statistics Page: 92 Qt rows; actual owned-engine/Window/worker:
12 Qt rows. All 48 previous unit suites remain in the GCC cohort.
At each 100/150/200% scale, six offline smoke rows and four real-engine Qt rows
pass, with six offline plus two actual engine captures (24 three-scale PNGs).
Hosted desktop 39144813366 and SDK 39144813345 pass on the exact source; the inspected
desktop artifact has 50 units and eight captures. Dist 103 hashes verify,
82 private runtime files are unchanged and the previous complete dist is retained.
No original profile/payload access or process signals; original HEAD/index unchanged.

This is one measured temporary libtorrent engine session per operation,
not application-wide global/session/lifetime/history parity. Ended counters are
the last observation, not exhaustive teardown totals. No Java ten-second smoothing,
IP/tracker/DHT, history, statistics writes/reset/mark, cross-run accumulation,
persistent-owner/full-client/plugin, seamless-switching or performance claim.

[Local verification](../evidence/20261018-statistics-engine/local-verification.json),
[hosted verification](../evidence/20261018-statistics-engine/hosted-verification.json),
[publication](../evidence/20261018-statistics-engine/publication.json), source-commit,
dist/RUNPATH receipts, exact test sets, source reviews and recipes are retained.
Complete rollback: `~/Java/BiglyBTC/build/statistics-engine-20261018/previous-dist`. Earlier P04-15 evidence is unchanged.
The one requested P04-16 milestone is complete; report the result to the user.
Do not automatically start a further milestone.
<!-- /p04-16-engine-statistics-completion -->
