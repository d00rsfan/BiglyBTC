# P04-18 Saved daily transfer history

State: implemented — bounded read-only saved UTC daily history qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `9d6023fca3b322dfbe5d92d1666b7f2120cd7752`.

Replace the Statistics > Transfers > History placeholder with a read-only UTC
daily browser: date selection, previous/next day, explicit refresh, payload daily
summary and 24 hourly rows for the original six traffic counters. Saved files are
`stats/YYYY/MM/DD.dat` in the already held profile. Do not write history, modify
configuration or payload, scan directories, read/write cache.dat, acquire another
lease, or start network activity or an engine. Keep My Torrents/Ctrl+1,
Statistics/Ctrl+5 and the existing saved totals and Activity views intact.

Interpret the frozen original v1 start/end and six-counter delta records. Parse
but do not sum session baselines. A positive start anchors reconstructed minutes;
each delta/end advances one minute, independently of the end record's wall time.
Duplicate minutes and signed adjustments retain original accounting semantics.
Group only observations within the selected UTC date into 24 hourly buckets;
validate then exclude out-of-day rollover records. Missing files and hours with
no observations remain distinct from observed zero. Empty/header-only files do
not invent traffic. All arithmetic is checked and all results immutable.

Native bounds: Gregorian dates 1970..9999, 1 MiB input, 8192 records and 512 bytes
per line. Native rejects unknown versions, malformed records, overflow and input
limits as a whole read. These stricter policies differ from Java's permissive
skip/wrap behavior and do not establish broad parser/runtime parity. Read fixed
directories through O_DIRECTORY/O_NOFOLLOW relative to the held profile, then a
bounded regular file; unsafe, changed, oversized or malformed sources fail
explicitly. No fallback to cache or fabricated partial totals.

Use the existing bounded worker and one active optional Statistics operation.
Coalesce pending history date edits, preserve independent saved-total refreshes,
bind results to session/request/date/page and reject stale/closed/profile replies.
History loads lazily on first selection; F5 refreshes the selected History view.
The existing bottom Refresh saved totals remains saved-only. New profiles clear
old history. Hidden/minimized views cache facts without label/model mutation;
show/restore renders current facts. Preserve plain-text bounds and Qt lifetime /
reentry guards. UI performs no I/O and introduces no timer or worker.

Characterize valid-file accounting with an isolated Java 17 oracle compiling the
actual frozen LongTermStatsBase source with bounded test stubs. Native tests must
compare original-runtime fixture results independently, and separately test the
stricter native errors. Verify held-lease path/cancellation/read-only behavior,
UI date/order/visibility/zero/error behavior, actual Window/worker navigation,
close/reopen, busy coalescing, profile changes and unchanged generated file bytes.
Retain all 53 existing unit suites. Use GCC 16, Clang 24, C++23, Qt 6.12.0,
leak-enabled ASan/UBSan, three-scale captures and standard hosted CI. Freeze full
upstream/native hashes; update the portable dist with a complete retained rollback,
verify installed ELF/RUNPATH and private runtimes, commit/push/tag, update the
registry, and checkpoint evidence without changing original HEAD/index.

This is a saved UTC day browser, not the original rolling hour/week/month/year /
all-time graph, cache service, live history writer, persistent global engine or
full-client/performance/seamless-switching qualification. These remain planned.
Do not start a further milestone after this one.

Root owns Window/Bridge/CMake/app wiring, worker integration tests, preview
fixtures and shared provenance/publication. Parallel workers own parser/adapter,
History UI and independent original-source/oracle audit. Durable work is in
`build/saved-history-20261019` in the original/native siblings; evidence is in
`migration/evidence/20261019-saved-history`. Only generated owned test data may be
temporary. Never inspect or interrupt the running original client/profile.

<!-- p04-18-saved-history-completion -->
## Completion evidence

Source `9db774659334daa2dbabd81762c819c108eb43b2`, metadata `fe09d1c9430a5a3c684d34be97a6661d08704c72`, annotated tag `milestone-saved-history-20261019`.
All 48 changed/new source files match the actual tested Git source map.
GCC 57 unit suites, Clang 8 focused suites and 11 leak-enabled ASan/UBSan suites
pass without skips; all 53 previous GCC units remain. The new projection's 21
cases and reader's 11 families pass. The 17 fixtures compare 425 original Java
day/hour queries, covering 2550 signed scalar results. The new History UI's 34
and actual Window/worker's 12 Qt rows pass; the retained Page's 92, Statistics
worker's 20, engine worker's 12 and graph worker's 7 rows also pass. At each scale
(100%, 150% and 200%), seven offline smoke rows, four actual engine rows and
four actual graph rows pass. Each scale produces 12 PNGs, including ready and
missing saved History, for 36 PNGs overall. Hosted desktop 39172584835 and SDK 39172584837
pass on the exact source; the independently inspected hosted artifact has 57
units and 12 captures. The dist's 103 file hashes verify; 82 private runtime
files are unchanged, and a complete
previous dist remains available. Original Java HEAD/index and live profile are
untouched; no original process was signaled. Offscreen captures give no manual
compositor acceptance credit.

This implements only the read-only saved UTC daily traffic browser:
date navigation and Refresh, observed daily payload totals and 24 hourly rows for six
signed counters. Actual baselines are ignored; positive anchors, reconstructed
minutes, duplicate records and signed adjustments retain valid-source accounting.
Unknown or missing hours remain distinct from observed zero. Version, record
shape, input-limit and overflow handling are deliberately stricter than Java;
those errors and presence
metadata have zero Java equality credit. Native full-day projection does not
apply the original reader's current-clock end clamp. This is not the rolling
hour/week/month/year/all-time graph, cache service, live history writer, global
history/engine ownership, full-client/plugin, seamless-switching or performance
parity. No history/configuration/payload writes, scans, cache.dat access, extra
lease, engine, timer or network activity are introduced by this reader/view.

[Local verification](../evidence/20261019-saved-history/local-verification.json),
[hosted verification](../evidence/20261019-saved-history/hosted-verification.json),
[publication](../evidence/20261019-saved-history/publication.json), full source map,
source-commit/dist/RUNPATH receipts, source reviews and the actual Java oracle are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/fe09d1c9430a5a3c684d34be97a6661d08704c72/milestones/saved-history-20261019.json).
Complete previous dist: `~/Java/BiglyBTC/build/saved-history-20261019/previous-dist`.
P04-17 remains qualified at source
`a84e4ecc6f78112c115c182aefc13bc4a1f3ac17`, metadata
`9d6023fca3b322dfbe5d92d1666b7f2120cd7752`, tag
`milestone-engine-activity-20261019`: 53 GCC, 5 Clang and 9 sanitizer suites;
52 engine-activity History rows, 26 View rows and 7 Window/worker rows; 92 Page
and 12 engine rows; and 30 captures across three scales. Its own prior dist rollback at
`~/Java/BiglyBTC/build/engine-activity-20261018/previous-dist` is unchanged.
P04-16 source `5fc34b588713112b126f3cebdfc891020301a281`, metadata
`30569e2631427003a4a92e7edee3572a3b34e740`, tag
`milestone-engine-statistics-20261018` and its 50 GCC, 3 Clang and 9 sanitizer
suites, with 24 captures, remain
historical, with `~/Java/BiglyBTC/build/statistics-engine-20261018/previous-dist`.
P04-15 and all earlier evidence, failed attempts and rollbacks are preserved.
The single requested P04-18 milestone is complete. Report the result to the user
and stop; do not automatically start another milestone.
<!-- /p04-18-saved-history-completion -->
