# P04-28 — Rolling Statistics history

Status: qualified and published (2026-10-21). Continues qualified P04-27; the old two-stage
stop is superseded by the user's request to finish Statistics.

Source baseline: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
TransferStatsView.refreshHistory and LongTermStatsBase.getTotalUsageInPeriod(Date,
Date), TimeFormatter fixed period constants. Native parent:
`0d3fc031d6bf97c39a4a7f6ced0c6151bc4af873`.

Implement Hour/Day/Week/Month/Year/All rolling history, raw minute observations,
independent whole-period totals, bounded held-profile reads and a fresh worker
result bound to the exact query/session/request/page. Preserve existing daily,
7-day,30-day browsers and all old counters/history tests. Separate actual six
fields into payload/protocol/DHT graph pairs. Never use daily sums to simulate
original query semantics: cross-midnight records and inclusive whole-period
endpoints can make whole totals differ from bucket sums.

Audits and contract are in `build/statistics-completion-20261021/`. Parallel
owners: Oracle pure core + inert Java oracle, Codec held-lease adapter, Driver
GUI; root integration, review, build/publication and provenance.

Acceptance: meaningful raw/planner/accumulator/security/UI/window-worker cases;
independent pinned original Java range-query comparison; GCC16,Clang24 and
leak-enabled ASAN/UBSAN; original baseline/index protected; local visual review;
portable dist updated only after tests. Full immutable source and implementation
hashes recorded separately after implementation commit. Broad component remains
partial: caches, recording/writes, persistent global/session/lifetime owner,
Tracker/DHT/Vivaldi/Tags/Country Stats/Counters providers are separate work.

## Current preview: P04-28 rolling Statistics history (2026-10-21)

Native source `0ecf3f3945bd810d6abc27a385f82b2317024b55`, metadata `f50a09bbefd8cee89ed8f0ecdb14240c7a226b7b`,
annotated tag `milestone-rolling-history-20261021` (object `659caba2c1fb0e0c6838e0ba5a4e35516a7dac91`).
GCC83, Clang34 and leak-enabled ASan/UBSan37 suites pass without skips.
All101 changed inputs and272 retained guards match the qualified source.
The two actual original-Java oracle executions cover7276 independent Date
queries /43656 six-field scalar comparisons; native results match. This is
query arithmetic evidence, not Java cache/writer/renderer or full-client parity.
All previous daily/seven-day/month tests and frozen Java fixtures are retained.
Local offscreen captures:78 at100%,150%,200%; hosted desktop
39595717199 and SDK39595717204
pass on this exact source, with83 units and26 API/digest/ZIP-byte-bound captures.
No real-compositor/user acceptance or performance improvement is claimed.

Run `~/Java/BiglyBTC/dist/BiglyBTC-preview`; Tools > Statistics / Ctrl+5,
Transfers > History > Rolling history offers Hour, Day, Week, fixed30-day Month,
365-day Year and All. Previous/Next select earlier periods; Refresh/F5 captures
a fresh explicit UTC now. Payload/Protocol/DHT charts retain signed values,
known zero and unobserved gaps; keyboard inspection and Copy use exact intervals.
Whole totals are independently queried, never summed from graph buckets.
Held-profile reads and All inventory are bounded and read-only. Reopening retains
scalar choices and reads fresh data. Six interval rates are now shown for the
existing operation-scoped engine; these are not persistent session totals.

Portable dist manifest SHA256 `44cefcf27bb44664475b9a0e31a8ff3a51c2a363222570720cbb5ab2719d605c`; all103 files
verify,82 runtime files remain byte-identical, and installed ELF bytes match
the qualified build after the exact CMake RUNPATH transformation. Complete prior
dist: `~/Java/BiglyBTC/build/statistics-completion-20261021/previous-dist`.
Original Java HEAD/index, running client and live profile remain untouched.

Next: P04-29 persistent native peer owner, then global Activity/Transfers
presentation and compatible lifetime/mark/history recording. Its engine overlay
is staged under `build/statistics-completion-20261021/next-accounting/`; it is
not integrated/qualified by this milestone. Full Activity/Transfers and all other
Statistics services remain open. User priority is fully Activity/Transfers first;
there are no pending questions. Continue the active request after checkpointing.

Evidence: [local verification](evidence/20261021-rolling-history/local-verification.json),
[hosted verification](evidence/20261021-rolling-history/hosted-verification.json),
[publication](evidence/20261021-rolling-history/publication.json).
Public [milestone](https://github.com/d00rsfan/BiglyBTC/blob/f50a09bbefd8cee89ed8f0ecdb14240c7a226b7b/milestones/rolling-history-20261021.json).
Earlier summaries below are historical and their bounded stop instructions were
superseded by the current request to finish Statistics.

