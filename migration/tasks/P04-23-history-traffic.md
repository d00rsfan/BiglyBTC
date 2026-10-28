# P04-23 Saved History traffic selection

State: implemented — saved History traffic selection qualified and published. Milestone 1 of the current three-milestone request.

See `build/statistics-next-two-20261020/contract.md` for frozen scope, API and ownership.
Original source baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; broad Java/UI/release parity remains open.
Qualify retained unit cohorts, Clang and leak-enabled ASan/UBSan; actual scaled UI
captures, exact-source hosted CI, portable dist rollback, public commits/tag and
private raw evidence checkpoint are required. Keep original running Java/profile untouched.

<!-- p04-23-history-traffic-completion -->
## Completion evidence

Source `1b08cceee82df9d29f1b35719a6ac24f2c6c2bc1`, metadata `bd27aeba82c48c8cd4bddcd8ca35d9f48fb7f310`, annotated tag `milestone-history-traffic-20261020`.
All 15 changed/new source files and 232 unchanged inputs
match the qualified byte maps. GCC 69 unit suites, Clang 20 focused suites and
23 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
67/18/21 names. New traffic data/View suites pass 10/14 actual
Qt rows. Retained navigation Page/Window rows remain 13/11, hourly resolution 22
and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12, Page 92, saved
Statistics worker 20, engine worker 12 and graph worker 7 rows remain qualified.
The 2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or UI equality credit is claimed. At each 100%, 150% and 200% scale,
ten offline smoke rows and four actual engine plus four graph rows pass, with
20 PNGs per scale and 60 overall. Hosted desktop 39442937594 and SDK 39442937460 pass on
the qualified source; the inspected artifact contains 69 units and 20 captures.
The metadata-only SDK workflow 39444099940 also passes on the
published metadata commit. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor acceptance.

Traffic selects Payload (default), Protocol or DHT independently for
Daily and seven-day saved History charts; the seven-day choice applies to both
daily totals and hourly resolution. Each graph copies the accepted saved signed
download/upload fields directly: Payload indices 3/1, Protocol 2/0 and DHT 5/4.
No combined sum, rate conversion, inferred zero or weekly aggregate is introduced.
Unknown buckets remain gaps, recorded zero stays real and signed corrections and
extremes retain their meaning. Tables still show the same six saved counters;
Daily summary labels and values remain explicitly payload even for another chart
traffic choice. Existing dates, resolutions and accepted result packets are unchanged.
A traffic change invalidates revision immediately and queues a guarded render
without another read, date/session/request change or model reset. Hidden/minimized
views defer rendering. Default calls continue to select Payload. Both traffic
choices join existing per-Window scalar navigation, including hidden choices,
and restore only on same nonzero-session tab reopening through fresh saved reads.
Accepted session advancement clears closed-tab memory; a visible Page keeps its
existing navigation/data-reset behavior. No widgets or saved result packets are
retained by this memory, and no configuration or restart persistence is added.
Core accounting, readers, workers, both prior Java corpora and their strict native
file/error policies remain unchanged. Seven file reads remain nontransactional.
Original Java UI/rendering equality, rolling history/global recording, keyboard
inspection and copy controls, full-client/plugin, seamless switching, performance
and broad release gates are not established by P04-23.

[Local verification](../evidence/20261020-history-traffic/local-verification.json),
[hosted verification](../evidence/20261020-history-traffic/hosted-verification.json),
[publication](../evidence/20261020-history-traffic/publication.json), committed
source audit/ranges, complete source maps and independent reviews are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/bd27aeba82c48c8cd4bddcd8ca35d9f48fb7f310/milestones/history-traffic-20261020.json).
P04-22 remains qualified at source
`f7d979801773934d481155019b18319b64beff96`, metadata `11dd37171a68c9de8bdae662cc2daf630f363120` and tag
`milestone-statistics-navigation-20261020`: 67 GCC, 18 Clang, 21 sanitizer suites, navigation 13/11
rows and 54 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-21 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-23 is milestone 1 of the current three-milestone request.
Continue with P04-24 keyboard chart inspection, then P04-25 copying selected
History chart values. Each requires its own reviewed scope, local/hosted
qualification, promoted portable preview with complete rollback, source/metadata/tag
publication and private checkpoint. Do not declare the current request complete
after P04-23; stop after all three authorized milestones are completed.
Root records the actual final private checkpoint separately, after complete
staged-byte verification before the initial ref update. No future/self commit is guessed here.
<!-- /p04-23-history-traffic-completion -->
