# P04-15 Tools > Statistics / Ctrl+5

State: bounded first Statistics implementation qualified, committed, pushed and tagged.
Source `be9ad3d5f409947445b6c253b17430a9df0f6655`, metadata `e9eef6cbf633207ae4b44691147e5ece4f5b03be`,
annotated tag `milestone-classic-statistics-20261018`. All27 changed/new source files match tested Git blobs.
GCC48 unit suites, Clang2 suites/86 core-reader cases, leak-enabled ASan/UBSan9
suites,35 Statistics page rows and20 Window/worker rows pass. Actual preview
smoke passes6 rows and6 captures at each100/150/200% scale. Standard hosted
desktop39140391574 independently confirms48 units/55 Statistics Qt rows/6 smoke
rows and6 captures; SDK39140391247 passes. Dist103 hashes verify,82 private runtime
files are unchanged and the previous complete dist is retained. No original
profile/payload access or process signals. Full global/session/history and other
original Statistics subviews remain unfinished; no whole-feature parity claim.

Parent native milestone: `8127336eada41e5795d6fdadaa42eea0ff555ec1`;
P04-14 implementation: `8efe56e4c0a4c46b8cdcb51035364db938494bb5`.
Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
The frozen source audit,35 complete Git/blob files and58 method bodies are in
`migration/evidence/20261018-statistics-next/`, manifest
`92dbafa6e3275bab32cf74d4a514ec7dcd07f501f991e615b162d5de0f8f2f5f`.

Implement one closeable Statistics tab, selected/reused through Tools > Statistics
and Ctrl+5 from any child. Preserve the My Torrents selection, filter, columns and
details; Ctrl+1 returns to that view. New Statistics pages start on Activity, then
Transfers, whose Current/History presentation distinguishes actual data from
unavailable capabilities. Close/reopen creates a fresh view and fresh read request;
late results for the old view cannot alter the replacement. Opening/refreshing
statistics never starts a transfer, DHT, tracker, timer-based sampler or plugin.

Activity may show only actual accepted per-transfer payload-rate observations,
clearly identified by transfer. Use at most600 observations, actual monotonic
observation times, and no synthetic zeros, interpolated samples or invented history.
Reset the series for another transfer. No samples means an empty chart and
unavailable rates. Hidden/minimized pages do not animate or repaint; there is no
additional sampling timer. Original global data-plus-protocol rates and global
history remain a distinct unfinished capability.

Transfers reads the original default fixed `BiglyBT.statistics` and its `.bak` /
`.saving` candidates under the existing held profile lease. Do not widen the
generic ConfigFile writer allowlist or initialize any Java/Core/manager service.
The pure owned-document projection retains13 raw signed-long facts, missing/type
provenance and unknown bytes. Whole bounds remain4MiB/100000nodes/depth64. Missing
files never become observed zero totals. Show last-saved overall, protocol/data
splits, separate DHT counters, uptime and admitted nonnegative since-mark values;
derive the original upper-baseline clamp without modifying the document. Missing,
invalid or negative operands remain unavailable. Do not sum combined totals and
their splits/DHT again. `mark_time` is epoch milliseconds, not a save timestamp;
uptime is seconds, counters are bytes. No source save timestamp is available.
Current global/session counters and long-term history remain unavailable.

Reader candidate policy: a syntactically complete dictionary Primary wins,
including an empty one. Wrong-shaped `all` fails that optional read rather than
silently selecting older data. Absent Primary tries Saving then Backup; malformed
Primary tries Backup then Saving. Unsafe/read/oversize errors fail safely. Only
all-three-observed-absent yields Missing; malformed candidates yield Failed.
No recovery rewrite, cleanup, reset/mark writer, extra lease, profile scan, payload
inspection, arbitrary filename or live original profile access.

Root owns shared CMake, worker/Window queues, app/smoke wiring and generated sample
data. Three parallel agents own pure projection/tests, fixed-leaf reader/tests,
and Statistics page/tests. Each new worker read binds immutable session/request
IDs to one live page generation; packets are delivered while busy, and optional
failure/cancellation never refreshes or revises the torrent library. Pending
requests coalesce and drain alongside saved-services and column reads/saves.

Qualification: pure13-field boundaries/malformed/bounds/ownership/since-mark;
reader all candidate/unsafe/cancellation/no-write cases; Qt page rendering and
bounded real telemetry; actual Ctrl+5/Tools/Ctrl+1/close/reopen/busy/stale/deletion
and queue integration. GCC16/Clang24 C++23, Qt6.12.0, leak-enabled sanitizers,
actual three-scale smoke and standard hosted CI. Preserve current checks/flags
and generated-profile isolation. No whole StatsView/plugin/client/global-history
or performance parity follows from this bounded first Statistics implementation.

Evidence: `migration/evidence/20261018-statistics/`; durable sibling work under
`build/statistics-20261018/`. Record implementation hashes only after an actual
immutable commit. Nonblocking questions belong in migration/QUESTIONS.md.

Final evidence: [local verification](../evidence/20261018-statistics/local-verification.json),
[hosted verification](../evidence/20261018-statistics/hosted-verification.json),
[publication](../evidence/20261018-statistics/publication.json), source-commit,
dist-promotion, full installed RUNPATH replay and nine independent source reviews.
Original HEAD/index remain unchanged. No test, fixture, flag or deadline relaxed.

Next Statistics work: connect owned persistent engine/session global data+protocol
counters, then qualify sampling lifetime/units/session accounting before global
Activity/history or the remaining service-specific subviews. Do not derive those
services from per-torrent payload or last-saved overall facts. This depends on the
unfinished P03 persistent session/owner integration; continue independent chapters
when that work cannot advance. No new blocking user question.
