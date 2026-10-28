# P04-20 Saved History charts

State: implemented — bounded saved History charts qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `94bf1cb1c17ea7979bc56ae21e9837605689f6a1`;
private parent `028b319961b4f412d8b41a69f92ce720774582a9`.

Add payload download/upload charts above the existing Daily and 7 days tables.
Daily uses 24 UTC hourly values; 7 days uses the seven independently read daily
values. Use exact signed int64 source facts, including real zero. Missing files,
empty/header-only files and unknown hours break traces rather than invent zero.
Only adjacent known buckets connect, and isolated observations remain visible.
The signed Y range includes zero; full int64 extrema must render without overflow.
Show UTC bucket labels, byte units, distinguishable legends and exact hover values.
No timer, animation, IO, extra requests, clock reads, lease, writer or worker.

Preserve existing history admission, stale request/session/date/page checks,
coalescing, F5, close/reopen and hidden/minimized deferred updates. Insert plot
updates into the existing guarded refresh before final model notifications;
guard deletion and direct reentry from tooltip/accessibility/model events.
Daily remains default. Keep all old tests and every prior 61/12/15 test name.
Use Qt6.12/C++23 and strict GCC16, Clang24 and leak-enabled ASan/UBSan checks.
Add data/widget/view tests and actual preview assertions; inspect all four saved
History captures at 100/150/200 percent and exact-source hosted CI.

Original Java characterizations and accounting stay unchanged. Audit committed
original payload graph sources, record exact differences for fixed UTC24/7
buckets versus rolling/pixel-merged original queries, and grant no new Java
execution or equality credit. Never fabricate a combined weekly total.

Promote the portable dist with its entire prior version preserved, unchanged
private runtimes and exact installed ELF/RUNPATH replay. No DEB or system install.
Commit/push/tag source and qualified metadata; update scoped provenance, keeping
broad first_ported/integrated fields null. Raw evidence uses scoped -text and a
complete pre-ref staged-byte guard on a private alternate index. Preserve original
HEAD/index and the running Java application/profile. Stop after this milestone.

Durable work: `build/history-charts-20261020` in both siblings. Evidence:
`migration/evidence/20261020-history-charts`. Only generated owned test inputs
may be temporary. API/ownership details are in the durable contract.md.

<!-- p04-20-history-charts-completion -->
## Completion evidence

Source `889dfe8cc9a22830149d79c7108ce946a7df2766`, metadata `775f84222031c1f43ee96e76c53fff0a5ef1e225`, annotated tag `milestone-history-charts-20261020`.
All 14 changed/new source files and 63 unchanged behavior
inputs match the qualified byte maps. GCC 64 unit suites, Clang 15 focused suites
and 18 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
61/12/15 names. Actual chart data/widget/view totals are 19/13/
22 Qt rows. Existing Daily 34/12, seven-day 38/12, Page 92, saved Statistics
worker 20, engine worker 12 and graph worker 7 rows remain qualified. The prior
2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or equality credit is claimed. At 100%, 150% and 200% scale, eight
offline smoke rows and four actual engine plus four graph rows pass, with 14 PNGs
per scale and 42 overall. Hosted desktop 39392956687 and SDK 39392956684 pass on this source;
the inspected artifact has 64 units and 14 captures. The dist's 103 files verify,
82 private runtime files are unchanged and the entire prior dist is retained.
Original Java HEAD/index, running client and live profile remain untouched.
Offscreen captures do not establish manual compositor acceptance.

This milestone only renders existing saved payload download/upload
facts: 24 UTC hourly buckets in Daily, or seven independent UTC daily-file totals
in 7 days. Unknown values are gaps, recorded zero is real and signed corrections
remain signed. Only adjacent known buckets connect; isolated observations remain
visible. Values are bytes, not rates. No weekly aggregate, interpolation, new
accounting, clock/timer, file read, worker, lease, network or persistence is added.
The original rolling/pixel-merged History queries, width scaling, local-zone axes,
overall period totals, cache/writer and global history service are not replicated.
Seven independently checked reads remain nontransactional. Existing strict native
errors, presence/counts and file safety retain their separate qualification.
Full-client/plugin, seamless switching, performance and broad release gates remain
unfinished.

[Local verification](../evidence/20261020-history-charts/local-verification.json),
[hosted verification](../evidence/20261020-history-charts/hosted-verification.json),
[publication](../evidence/20261020-history-charts/publication.json), committed
source audit/ranges, full source maps and independent reviews are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/775f84222031c1f43ee96e76c53fff0a5ef1e225/milestones/history-charts-20261020.json).
P04-19 remains qualified at source
`136af049bc3ea5307a666ef4b716eaa50d36d1c4`, metadata `94bf1cb1c17ea7979bc56ae21e9837605689f6a1` and tag
`milestone-history-week-20261020`: 61 GCC, 12 Clang, 15 sanitizer suites and 42 captures.
Its complete dist is retained at `~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. Its own earlier rollback
at `~/Java/BiglyBTC/build/history-week-20261020/previous-dist` and P04-18/P04-17/
P04-16 evidence, commits, tags and rollback directories remain unchanged.
The single requested milestone is complete; report and stop. Root records the
actual final private checkpoint separately, after complete staged-byte verification
before the initial private ref update. No future/self commit is guessed here.
<!-- /p04-20-history-charts-completion -->
