# P04-24 Keyboard inspection of saved History charts

State: implemented — saved History keyboard inspection qualified and published. Milestone 2 of the current three-milestone request.

See `build/statistics-next-two-20261020/contract.md` for frozen scope, API and ownership.
Original source baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; broad Java/UI/release parity remains open.
Qualify retained unit cohorts, Clang and leak-enabled ASan/UBSan; actual scaled UI
captures, exact-source hosted CI, portable dist rollback, public commits/tag and
private raw evidence checkpoint are required. Keep original running Java/profile untouched.

<!-- p04-24-history-keyboard-completion -->
## Completion evidence

Source `07219078bf3c5b3ed1cca7e701dcbdfeda8e687d`, metadata `05d460afde14e9a3510fe2f6d823422fe4edd642`, annotated tag `milestone-history-keyboard-20261020`.
All 7 changed/new source files and 241 unchanged inputs
match the qualified byte maps. GCC 70 unit suites, Clang 21 focused suites and
24 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
69/20/23 names. The new keyboard suite passes 17 actual Qt rows. Retained
traffic data/View rows remain 10/14, navigation Page/Window 13/11, hourly resolution
22 and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12, Page 92, saved
Statistics worker 20, engine worker 12 and graph worker 7 rows remain qualified.
The 2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or UI equality credit is claimed. At each 100%, 150% and 200% scale,
eleven offline smoke rows and four actual engine plus four graph rows pass, with
21 PNGs per scale and 63 overall. Hosted desktop 39448068109 and SDK 39448068100 pass on
the qualified source; the inspected artifact contains 70 units and 21 captures.
The metadata-only SDK workflow 39449473199 also passes on the
published metadata commit. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor or real screen-reader acceptance.

A focused, visible saved History chart accepts unmodified Left/Right/Home/End
for exact UTC bucket inspection. Daily, seven-day totals and seven-day hourly
charts expose 24, 7 and 168 selectable buckets, including unknown gaps. First
Left/Right/Home selects the first bucket; End selects the last. Subsequent keys
clamp without wrapping. An elided caption shows the selection; the full tooltip and accessible
description identify the date/hour, chosen traffic and exact signed
download/upload values.
Unknown remains Unknown, recorded zero stays real and signed extremes are not
rounded by narration. Focus loss retains selection/narration but hides focus-only
guides and known point outlines. Unknown has no invented Y point. Hover retains
its existing tooltip behavior without moving the keyboard selection. Accepted
frame replacement clears selection. Reentrant frame/key/property/accessibility
callbacks are fenced by object lifetime, frame identity and interaction revision.
These are native offscreen behavior checks, not real assistive-technology acceptance.
Keyboard inspection adds no read, request, timer, worker, configuration write or
retained source packet. It is per-frame interaction state, not added to the
same-session scalar navigation record. Existing per-Window dates, tabs, resolution
and independent Daily/seven-day traffic choices still restore through fresh reads.
Accepted session advancement clears closed-tab memory; a visible Page keeps its
existing navigation/data-reset behavior. No widgets or saved result packets are
retained, and no restart persistence is added. Traffic still copies exact saved
fields: Payload indices 3/1, Protocol 2/0 and DHT 5/4, without combined sums, rates
or additional reads. Tables and explicitly payload summaries are unchanged.
Core accounting, readers, workers, both original Java corpora and strict native
file/error policies remain unchanged. Seven daily file reads are nontransactional.
No weekly aggregate, new Java execution or original Java UI equality is claimed.
Copy controls belong to P04-25. Rolling/global history, full-client/plugin parity,
seamless profile switching, performance and broad release gates remain open.

[Local verification](../evidence/20261020-history-keyboard/local-verification.json),
[hosted verification](../evidence/20261020-history-keyboard/hosted-verification.json),
[publication](../evidence/20261020-history-keyboard/publication.json), committed
source audit/ranges, complete source maps and independent reviews are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/05d460afde14e9a3510fe2f6d823422fe4edd642/milestones/history-keyboard-20261020.json).
P04-23 remains qualified at source
`1b08cceee82df9d29f1b35719a6ac24f2c6c2bc1`, metadata `bd27aeba82c48c8cd4bddcd8ca35d9f48fb7f310` and tag
`milestone-history-traffic-20261020`: 69 GCC, 20 Clang, 23 sanitizer suites, traffic data/View
rows 10/14 and 60 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-keyboard-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`,
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-22 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-24 is milestone 2 of the current three-milestone request.
Continue only with P04-25 copying selected History chart values. It requires its
own reviewed scope, actual local/hosted qualification, promoted portable preview
with complete rollback, source/metadata/tag publication and private checkpoint.
The current request remains active; do not declare it complete after P04-24.
Stop after P04-25 completes the third and final authorized milestone.
Root records the actual final private checkpoint separately, after complete
staged-byte verification before the initial ref update. No future/self commit is guessed here.
<!-- /p04-24-history-keyboard-completion -->
