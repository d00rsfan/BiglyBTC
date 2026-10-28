# P04-19 Saved seven-day overview

State: implemented — bounded independent saved seven-day overview qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `fe09d1c9430a5a3c684d34be97a6661d08704c72`;
private parent `ac1d95f97b2dfeec2110d790549ffb8b16a038ef`.

Add Daily / 7 days inside Statistics > Transfers > History. Daily remains the
default and retains its APIs, controls and qualification. The new overview reads
exactly seven fixed UTC daily history paths ending at a selected date through
the already held lease. Show seven dated rows, source state and the original six
signed counters. Missing files, present empty files and observed zero remain
distinct. Provide previous/next seven-day navigation and Refresh/F5.

Do not invent a weekly total. The original Java whole-period query can include
midnight spill records from another source day, whereas independent daily reads
clip each file to its own date. This milestone intentionally presents the latter
with the already qualified daily accounting. Original rolling 168-hour graphs,
clock-clamped sliding/calendar periods, cache and live writer remain planned.

Use pure checked Gregorian date arithmetic,1970..9999 with a full seven-day
range, and reuse the existing strict bounded daily reader sequentially. One
malformed/unsafe/changed/read/cancel failure rejects the whole batch without a
published partial result. Do not scan, read cache.dat, write files, acquire a
second lease or start network services. Seven reads are individually checked;
they are not a transactional multi-file snapshot.

Use the existing optional Statistics worker slot. Daily and seven-day requests
coalesce as one latest pending history query; saved totals stay independent.
Bind replies to kind/session/request/end-date/page. Preserve stale rejection,
close/reopen behavior, profile transitions, hidden/minimized deferred rendering,
Qt reentry/lifetime guards, and My Torrents/Ctrl+1. Closing a view must not cancel
the operation. The bottom saved-total refresh remains saved-only.

Add independent original-Java daily row/hour characterization on generated past
seven-day profiles, and separate strict native/presence tests. Retain all57
previous GCC unit names and their assertions. Qualify new date, adapter, UI and
Window/worker behavior with GCC16, Clang24, C++23, Qt6.12 and leak-enabled
ASan/UBSan; capture ready/missing states at100/150/200% and verify hosted CI.

Freeze full source/Git hashes. Promote the portable dist with the entire previous
dist retained, unchanged private runtimes and exact installed ELF/RUNPATH replay.
No DEB or system installation. Commit/push/tag qualified source and metadata,
update component provenance without advancing broad integration fields, and
checkpoint evidence with scoped raw-byte attributes and complete staged-byte
verification BEFORE private ref advancement. Preserve original HEAD/index and
the running Java application/profile throughout. Stop after this one milestone.

Durable work: `build/history-week-20261020` in both siblings. Evidence:
`migration/evidence/20261020-history-week`. Only generated owned test inputs may
be temporary. Detailed shared API/ownership contract is in the durable build
directory; shared lock and publication are root-owned.

<!-- p04-19-history-week-completion -->
## Completion evidence

Source `136af049bc3ea5307a666ef4b716eaa50d36d1c4`, metadata `94bf1cb1c17ea7979bc56ae21e9837605689f6a1`, annotated tag `milestone-history-week-20261020`.
All 49 changed/new source files match the actual qualified Git map.
GCC 61 unit suites, Clang 12 focused suites and 15 leak-enabled ASan/UBSan suites
pass without skips, retaining every prior 57/8/11 test name. The new date policy's
9 cases and reader's 12 families pass. Three generated profiles characterize
525 independent original-Java member-day/hour queries and 3150 signed scalars.
The earlier daily corpus remains unchanged: 17 files, 425 queries and 2550 scalars.
New seven-day UI: 38 Qt rows; actual Window/worker: 12 rows. Existing Daily UI's
34 and worker's 12 rows, Page's 92, saved Statistics worker's 20, engine worker's
12 and graph worker's 7 rows remain qualified. At each 100%, 150% and 200% scale,
eight offline smoke rows, four actual engine rows and four actual graph rows
pass, producing 14 PNGs per scale, 42 overall. Hosted desktop 39387664142 and SDK
39387664175 pass on the exact source; the independently inspected artifact has 61 units
and 14 captures. The dist's 103 file hashes verify; 82 private runtime files are
unchanged and the complete previous dist is retained. Original Java HEAD/index,
process and live profile are untouched. Offscreen evidence does not establish
manual compositor acceptance.

This is a seven-row overview of independent saved UTC daily files,
ending at a selected date. No weekly total is computed. A whole original multi-day
query can include rollover spill from a prior pathname day; seven independently
clipped member queries are not claimed equal to that aggregate. Original rolling
168-hour graphs, sliding/configured calendar weeks, current-clock end clamp,
cache and live writer are not ported. Missing files, present empty/header-only
files, unknown totals, observed zero and signed adjustments stay distinct.
Native strict shape/version/limits/checked arithmetic and presence/count metadata
receive zero Java equality credit. One failed or cancelled member rejects the
entire batch without partial publication. Seven individually checked reads are
not a transactional multi-file snapshot. No history/config/payload writes,
directory scans, cache.dat, extra lease, new worker, engine or network owner are
introduced. Full-client/plugin, seamless-switching, performance and release gates
remain unfinished.

[Local verification](../evidence/20261020-history-week/local-verification.json),
[hosted verification](../evidence/20261020-history-week/hosted-verification.json),
[publication](../evidence/20261020-history-week/publication.json), full source map,
dist/RUNPATH receipts, exact test sets and actual Java source/class/stdout records
are retained. [Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/94bf1cb1c17ea7979bc56ae21e9837605689f6a1/milestones/history-week-20261020.json).
Complete previous dist: `~/Java/BiglyBTC/build/history-week-20261020/previous-dist`.
P04-18 remains qualified at source
`9db774659334daa2dbabd81762c819c108eb43b2`, metadata `fe09d1c9430a5a3c684d34be97a6661d08704c72` and tag
`milestone-saved-history-20261019`: 57 GCC, 8 Clang and 11 sanitizer suites, Daily UI 34
and worker 12 rows, 2550 original-Java scalars and 36 three-scale captures.
Its complete preview is the new rollback above. Its own previous dist at
`~/Java/BiglyBTC/build/saved-history-20261019/previous-dist` and all P04-18 evidence,
including the preserved failed private checkpoint and scoped raw-byte repair,
remain unchanged. P04-17/P04-16 and earlier evidence, source hashes, tags and
rollback directories also remain historical and unchanged.
The single requested P04-19 milestone is complete; report and stop. Do not
automatically start another chapter. Root records actual final private checkpoint
blobs separately, with complete staged-byte verification before ref advancement.
<!-- /p04-19-history-week-completion -->
