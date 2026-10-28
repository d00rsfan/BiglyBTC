# P04-25 Copy selected History chart values

State: implemented — explicit saved History Copy qualified and published. Milestone 3 of the completed three-milestone request.

After keyboard inspection, add explicit copying of the selected UTC bucket,
traffic family and exact signed/unknown download/upload values. No automatic
clipboard changes, file/network/configuration writes or inferred values. Test
with the isolated offscreen clipboard; keep the running Java and profile untouched.
The frozen API and acceptance contract remain recorded below.

The detailed contract remains frozen in
`build/statistics-next-two-20261020/copy-contract.md`. The independently reviewed
implementation and tests are qualified, installed and published at the exact
source/metadata/tag recorded in the completion evidence below.

The chart owns a **Copy selected values** action, available through its context
menu and widget-local Copy shortcut. It is disabled until the current frame has
a selected bucket, including an unknown bucket. Accepted frame replacement
clears selection and disables the action; losing focus retains selection.
An explicit invocation copies the selected immutable frame's full UTC bucket
description, chosen traffic and exact signed values or Unknown as plain text to
the normal Clipboard. Hover, focus, selection and frame changes never copy.
Selection and FindBuffer clipboard modes remain untouched. No new saved read,
request, timer, file/configuration write or retained source packet is added.

Qualification must retain every P04-24 suite and add the Copy suite, inspect
normal hosted artifacts and three scaled generated-profile previews, retain the
complete prior portable dist and publish source/metadata/tag plus the private
checkpoint. Clipboard tests use the isolated offscreen platform with MIME-state
restoration; they do not establish real compositor or screen-reader acceptance.
This native interaction adaptation does not advance broad original Java
coverage or establish full-client, plugin, performance or seamless-switching
parity. Finish this third milestone and stop the current request.

<!-- p04-25-history-copy-completion -->
## Completion evidence

Source `ab0ff75d81300d8d3f9e930a9bf55491c1801400`, metadata `3635fe01fa0212e075af5c956740e499303980b9`, annotated tag `milestone-history-copy-20261020`.
All 6 changed/new source files and 243 unchanged inputs
match the qualified byte maps. GCC 71 unit suites, Clang 22 focused suites and
25 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
70/21/24 names. The new Copy suite passes 19 actual Qt rows. Retained keyboard
rows remain 17, traffic data/View 10/14, navigation Page/Window 13/11, hourly
resolution 22 and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12,
Page 92, saved Statistics worker 20, engine worker 12 and graph worker 7 rows remain
qualified. The 2550 and 3150 original-Java scalar comparisons remain unchanged;
no new Java execution or UI equality credit is claimed. At each 100%, 150% and
200% scale, twelve offline smoke rows and four actual engine plus four graph rows
pass, with 22 PNGs per scale and 66 overall. Hosted desktop 39450607631 and SDK 39450607673
pass on the qualified source; the inspected artifact contains 71 units and 22
captures. The metadata-only SDK workflow 39452631555 also passes
on the published metadata commit. The dist's 103 files verify, 82 private runtime
files are unchanged and the entire prior dist is retained. Original Java
HEAD/index, running client and live profile remain untouched. Offscreen clipboard,
keyboard and captures do not establish real compositor/system clipboard or
screen-reader acceptance.

A saved History plot exposes Copy selected values through its context menu
and the normal Copy shortcut (Ctrl+C on the tested Linux platform), scoped to the
focused widget. It starts disabled and enables only for a valid selected current
bucket, including Unknown. Focus loss retains the selection and context action.
Accepted frame replacement clears selection and disables Copy; a fresh reopened
Page begins without a per-frame selection. No user action means no clipboard change.
Explicit Copy publishes exactly the immutable selected bucket's existing tooltip:
UTC day/hour, Payload/Protocol/DHT family and signed decimal downloaded/uploaded
bytes or Unknown. It does not copy the currently hovered bucket by accident,
convert units, infer zero, build HTML, combine counters or calculate a weekly sum.
Only QClipboard::Clipboard is written, never Selection or FindBuffer. QAction
changed-signal and clipboard-notification reentry/deletion paths use object,
frame, interaction revision and selected-index guards; the setter is the final
receiver operation. Arbitrary deletion from Qt-internal ActionChanged event filters
is outside the reviewed signal-callback qualification seam. Generated tests use
the isolated offscreen clipboard with RAII restoration, not a real user clipboard.
Copy adds no saved read, timer, worker, file/config write or retained source packet.
Existing Left/Right/Home/End inspection clamps across 24/7/168 buckets, preserves
signed/Unknown narration and clears on accepted frame replacement. Dates, tabs,
resolution and independent Daily/seven-day traffic choices still restore within
the same nonzero profile session through fresh reads. Closed-tab memory clears
on accepted session advancement; visible-Page session behavior remains unchanged.
Core accounting, readers, workers, both original Java corpora and strict native
file/error policies are unchanged. Seven file reads remain nontransactional.
No new Java execution/UI equality, global/rolling history, restart persistence,
seamless profile switching, full-client/plugin parity, performance or broad release
qualification is established. This completes only the three requested UI milestones.

[Local verification](../evidence/20261020-history-copy/local-verification.json),
[hosted verification](../evidence/20261020-history-copy/hosted-verification.json),
[publication](../evidence/20261020-history-copy/publication.json), committed
source audit/ranges, complete source maps and independent reviews are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/3635fe01fa0212e075af5c956740e499303980b9/milestones/history-copy-20261020.json).
P04-24 remains qualified at source
`07219078bf3c5b3ed1cca7e701dcbdfeda8e687d`, metadata `05d460afde14e9a3510fe2f6d823422fe4edd642` and tag
`milestone-history-keyboard-20261020`: 70 GCC, 21 Clang, 24 sanitizer suites, keyboard 17 rows
and 63 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-copy-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/history-keyboard-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`,
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-23 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-23 traffic selection, P04-24 keyboard inspection and P04-25 explicit Copy
are the three completed milestones of this request. Stop this request after its
actual final private checkpoint; do not begin a fourth milestone without new user
direction. The broader migration and release epic remain unfinished. Open work is
recorded separately; no current user request or release-parity completion is inferred.
Root records the actual final private checkpoint separately, after complete
staged-byte verification before the initial ref update. No future/self commit is guessed here.
<!-- /p04-25-history-copy-completion -->
