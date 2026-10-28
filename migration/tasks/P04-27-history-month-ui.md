# P04-27 Tools > Statistics > Transfers > History >30 days

State: implemented — bounded P04-27 qualified and published; milestone 2 of two. Final private checkpoint recorded separately.

Contract: `build/history-thirty-next-two-20261020/contract.md`.

Owner/integrator: root; independent staged source/test reviews. Phase P04,
component `statistics.history_month_ui`, feature `F21.history_month_ui`. Depends on qualified P04-25;
P04-27 additionally requires actual completed P04-26.

Use original BiglyBT commit `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
LongTermStats, TimeFormatter and TransferStatsView source references.
Fixed30 independent UTC daily records, preserving original daily parser/policies;
no current-clock rolling graph/aggregate or new Java equality claim.
Original running client/profile/payloads untouched; generated tests only.
Qt6.12/C++23/GCC16/Clang24/leak-enabled ASan+UBSan; no installation needed.

Acceptance: reviewed exact source/fixtures, retained prior suites, negative and
lifetime/cancellation cases, normal hosted CI with independently inspected
artifacts, three-scale UI regressions, portable dist with complete rollback,
pushed source/metadata/tag and raw-byte verified private checkpoint.
Record actual full implementation/provenance hashes only after qualification.
Stop after P04-27 completes both milestones. Full migration/release stays open.

<!-- p04-27-history-month-ui-completion -->
## P04-27: Statistics: 30 independent saved UTC days

Source `ddc6bb022095866eceeddf2d8598201e07652c28`, metadata `0d3fc031d6bf97c39a4a7f6ced0c6151bc4af873`,
annotated tag `milestone-history-month-ui-20261020`. All 24 source files and
238 unchanged inputs match their qualified maps.
GCC/Clang/leak-enabled ASan+UBSan suites pass 76/27/30 with zero skips, retaining
every predecessor 73/24/27 unit name. New suites: saved-history-month-plot-data: 13 actual Qt rows, saved-history-month-page: 25 actual Qt rows, saved-history-month-window-worker: 12 actual Qt rows.
Retained daily/seven-day original-Java scalar comparisons remain 2550/3150;
keyboard 17, Copy 19 and all earlier named regression suites remain required.
No new Java execution or UI-equality credit is assigned. At 100%,150%,200%,
13 offline smoke rows and four engine plus four graph rows pass per scale:
23 PNGs per scale, 69 local captures overall.
Hosted desktop 39461888661 and SDK
39461888560 pass on the exact source, with an API/digest/
ZIP/tree-byte-bound artifact containing 76 unique units and
23 captures. Metadata SDK 39463131398 passes on the
published metadata commit. Dist verifies all 103 files, 82 runtime files remain
byte-identical, exact installed RPATH replay passes, and the complete prior dist
is retained. Original Java HEAD/index, running client and live profile remain untouched.

Tools > Statistics > Transfers > History > 30 days displays 30 fixed rows with
saved state plus the six raw counters. Its chart has 30 daily buckets and chooses
Payload, Protocol or DHT without another read or a combined sum. Previous/Next
move 30 days; ending dates span 1970-01-30..9999-12-31. Keyboard inspection and
explicit Copy reuse the qualified immutable-frame behavior, including Unknown
and signed values. Hidden/minimized rendering is deferred. Generation/session/
kind/date gates, one shared active optional-statistics slot, coalescing, cancellation,
close/reopen and deletion handling are qualified only within this owned preview.
Same-session navigation retains scalar month date/traffic choices; reopening
uses fresh reads and keeps no widget, packet or cursor in navigation memory.
No restart/configuration persistence or new live history service is implied.

The native range is exactly 30 independent UTC daily records, not a variable
calendar month or an aggregate. Missing files, present files without observations,
known zero and signed adjustments remain distinct; all six saved counter
components are retained. Dates are oldest first and end inclusively, wholly
within 1970..9999. One held lease is borrowed and the unchanged daily reader is
called sequentially. Malformed, unsafe, IO and cancellation failures produce no
partial successful range. The reads are not a transactional multi-file snapshot.
No payload stat/read, directory scan, clock clamp, cache, writer, global recorder
or additional lease is added. Original TS_MONTH selects 720 hourly periods before
pixel merging; this native 30-daily-record view has different resolution and no
original monthly graph/query/UI equality claim. The original 425/2550 and 525/3150
receipts remain in their historical namespaces, unchanged. Broad first_ported and
integrated_through coverage remains null. Full-client/plugin, switching, rolling
global history, performance and broad release qualification remain unfinished.

The immediate predecessor is source `d36dd2d1c92914c796e20115939e3c66e7627ba1`,
metadata `e53678361d8903f1e34b0a0c39623c78bd7dff7c`, tag `milestone-history-month-backend-20261020`; its whole dist is
retained at `~/Java/BiglyBTC/build/history-month-ui-20261020/previous-dist`. Earlier receipts, failures, source capsules, commits,
tags, checkpoints and rollback directories stay unchanged. The predecessor's
actual private checkpoint `b1b72df3534bb16c9b7abdc66b897805d3435e3d` is this phase's parent;
this phase's final private identity is recorded separately after staged-byte proof.

Both authorized milestones, P04-26/P04-27, are qualified and published.
Root records the actual final raw-byte private checkpoint after these updates,
then stops this request. No third milestone is authorized by this request.
No future or self-referential private commit is invented. The broader migration
and release epic remain open; further work awaits new user direction.

Evidence: [local verification](../evidence/20261020-history-month-ui/local-verification.json),
[hosted verification](../evidence/20261020-history-month-ui/hosted-verification.json) and
[publication](../evidence/20261020-history-month-ui/publication.json).
Public [milestone](https://github.com/d00rsfan/BiglyBTC/blob/0d3fc031d6bf97c39a4a7f6ced0c6151bc4af873/milestones/history-month-ui-20261020.json).
Recipes remain in `build/history-month-ui-20261020` and the shared documentation helper in
`build/history-thirty-next-two-20261020/finalize_docs.py`.
<!-- /p04-27-history-month-ui-completion -->
