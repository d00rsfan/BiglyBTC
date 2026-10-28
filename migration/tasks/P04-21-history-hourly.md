# P04-21 Seven-day hourly History resolution

State: implemented — bounded seven-day hourly History resolution qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `775f84222031c1f43ee96e76c53fff0a5ef1e225`;
private parent `e78c6e77c171c6fd067cbe4b8c180ef1a384ad09`.

Add Graph resolution: Daily totals / Hourly values to the 7 days History view.
Daily totals remains default; both modes keep the existing seven-row daily table.
Hourly resolution exposes exactly 168 UTC hours from the seven selected dates,
using accepted daily snapshots and signed payload download/upload values.
Unknown hours/files stay gaps, recorded zero remains real, adjacent known hours
connect and isolated observations retain markers. Cross-midnight adjacency and
exact date/hour labels must remain correct. Use sparse UTC date/hour ticks,
exact hover facts, full signed bounds and finite rendering at narrow widths.

Resolution is local rendering preference: switching performs no read, request,
clock/timer, worker, lease, network, accounting or persistence. Keep the selected
date and accepted frame. Hidden/minimized changes defer chart/model/tooltip
updates; stale replies and tooltip/accessibility reentry cannot replace newer
facts or mode. Preserve Daily view, all old test files/assertions and table IDs.
F5/date refresh still uses existing coalesced worker behavior. No weekly sum,
current-clock rolling query or new original Java equality credit is claimed.

Qualify all prior64 GCC/15 Clang/18 sanitizer names plus the new hourly suite,
strict C++23/Qt6.12 flags and leak-enabled ASan/UBSan. Observe actual new Qt rows.
Extend the existing8-row smoke without extra reads; retain14 old captures and
add hourly ready/missing captures,16 per scale at100/150/200 percent,48 local.
Inspect exact-source hosted65-unit/16-capture artifact plus normal SDK CI.
Keep both frozen Java accounting corpora and parser/reader/worker files exact.

Promote the portable103-file dist with82 unchanged runtimes, complete old dist
rollback and exact installed RPATH byte proof. No DEB or system installation.
Commit/push source then qualified metadata/tag. Scoped source provenance must
retain broad first_ported/integrated null. Raw evidence uses -text and complete
staged-byte guards before the private alternate-index ref update. Preserve
original HEAD/index and the running Java application/profile. Stop after one.

Durable work: `build/history-hourly-20261020` in both siblings. Evidence:
`migration/evidence/20261020-history-hourly`. Only owned generated test inputs
may be temporary. API/ownership details are in the durable contract.md.

<!-- p04-21-history-hourly-completion -->
## Completion evidence

Source `c35ca6e2470aa8f154ec7676e61734c012725283`, metadata `42b45749abd481014486a33844520c5d670b534b`, annotated tag `milestone-history-hourly-20261020`.
All 10 changed/new source files and 202 unchanged inputs
match the qualified byte maps. GCC 65 unit suites, Clang 16 focused suites and
19 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
64/15/18 names. The new hourly-resolution suite passes 22 actual Qt rows;
prior chart data/widget/view totals remain 19/13/22. Existing Daily 34/12,
seven-day 38/12, Page 92, saved Statistics worker 20, engine worker 12 and graph
worker 7 rows remain qualified. The 2550 and 3150 original-Java scalar comparisons
remain unchanged; no new Java execution or equality credit is claimed. At each
100%, 150% and 200% scale, eight offline smoke rows and four actual engine plus
four graph rows pass, with 16 PNGs per scale and 48 overall. Hosted desktop
39397413125 and SDK 39397413183 pass on this source; the inspected artifact contains
65 units and 16 captures. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor acceptance.

Graph resolution changes only the saved seven-day chart.
Daily totals remains default; Hourly values displays exactly 168 fixed UTC
buckets, oldest selected date at 00:00 through ending date at 23:00. Values copy
optional payload download/upload bytes directly from accepted saved hours; no
daily-total division, rate, sum or weekly aggregate is inferred. Unknown days or
hours are gaps, recorded zero is real and signed corrections remain signed.
Adjacent known hours connect across midnight, with isolated points visible.
The seven-row daily table and date/session/request admission remain unchanged.
Resolution is local to the View lifetime, not configuration persistence. A control
change invalidates the previous render immediately; its replacement is queued until
the control's signal delivery returns. A destroyed View receives no queued render.
Switching adds no read, clock, timer, worker, lease, network, writer/cache or accounting.
The original current-clock, hour-rounded, pixel-merged week graph and global
history service are not replicated. Existing strict errors and presence/file
safety remain separately qualified. Seven reads remain nontransactional.
Full-client/plugin, seamless switching, performance and broad release gates
remain unfinished.

[Local verification](../evidence/20261020-history-hourly/local-verification.json),
[hosted verification](../evidence/20261020-history-hourly/hosted-verification.json),
[publication](../evidence/20261020-history-hourly/publication.json), committed
source audit/ranges, complete source maps and independent reviews are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/42b45749abd481014486a33844520c5d670b534b/milestones/history-hourly-20261020.json).
P04-20 remains qualified at source
`889dfe8cc9a22830149d79c7108ce946a7df2766`, metadata `775f84222031c1f43ee96e76c53fff0a5ef1e225` and tag
`milestone-history-charts-20261020`: 64 GCC, 15 Clang, 18 sanitizer suites, chart rows 19/13/22
and 42 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist`. Its own
earlier rollback at `~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`
and all P04-19/P04-18/P04-17/P04-16 evidence, commits, tags, diagnostic failures
and rollback directories remain unchanged.
The single requested milestone is complete; report and stop. Root records the
actual final private checkpoint separately, after complete staged-byte verification
before the initial ref update. No future/self commit is guessed here.
<!-- /p04-21-history-hourly-completion -->
