# P04-26 Read-only fixed30-day backend

State: implemented — bounded P04-26 qualified and published; milestone 1 of two. Final private checkpoint recorded separately.

Contract: `build/history-thirty-next-two-20261020/contract.md`.

Owner/integrator: root; independent staged source/test reviews. Phase P04,
component `statistics.saved_history_month`, feature `F20.saved_history_month`. Depends on qualified P04-25;
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

<!-- p04-26-saved-history-month-completion -->
## P04-26: Read-only fixed 30-day saved-history backend

Source `d36dd2d1c92914c796e20115939e3c66e7627ba1`, metadata `e53678361d8903f1e34b0a0c39623c78bd7dff7c`,
annotated tag `milestone-history-month-backend-20261020`. All 9 source files and
246 unchanged inputs match their qualified maps.
GCC/Clang/leak-enabled ASan+UBSan suites pass 73/24/27 with zero skips, retaining
every predecessor 71/22/25 unit name. New suites: saved-history-month-dates: 11 actual case families, saved-history-month-reader: 14 actual case families.
Retained daily/seven-day original-Java scalar comparisons remain 2550/3150;
keyboard 17, Copy 19 and all earlier named regression suites remain required.
No new Java execution or UI-equality credit is assigned. At 100%,150%,200%,
12 offline smoke rows and four engine plus four graph rows pass per scale:
22 PNGs per scale, 66 local captures overall.
Hosted desktop 39457365591 and SDK
39457365491 pass on the exact source, with an API/digest/
ZIP/tree-byte-bound artifact containing 73 unique units and
22 captures. Metadata SDK 39458345089 passes on the
published metadata commit. Dist verifies all 103 files, 82 runtime files remain
byte-identical, exact installed RPATH replay passes, and the complete prior dist
is retained. Original Java HEAD/index, running client and live profile remain untouched.

This milestone adds only the pure date-range and held-profile reader backend.
The runnable desktop still exposes the previously qualified Daily and 7 days
views; the 30-day tab and worker/UI integration are P04-27, not credited here.
The retained desktop, keyboard, explicit Copy and engine regressions were rerun;
their existing features are not new backend functionality.

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

The immediate predecessor is source `ab0ff75d81300d8d3f9e930a9bf55491c1801400`,
metadata `3635fe01fa0212e075af5c956740e499303980b9`, tag `milestone-history-copy-20261020`; its whole dist is
retained at `~/Java/BiglyBTC/build/history-month-backend-20261020/previous-dist`. Earlier receipts, failures, source capsules, commits,
tags, checkpoints and rollback directories stay unchanged. The predecessor's
actual private checkpoint `0666c1846d86367c2ec7eb0c119382e85d79148b` is this phase's parent;
this phase's final private identity is recorded separately after staged-byte proof.

Milestone 1 of the two requested milestones is qualified and published.
Root must record its actual raw-byte-verified private checkpoint after these
documentation updates; P04-27 may start only from that completed milestone.
Continue only with the authorized30-day UI milestone, then stop after its final
checkpoint. No broader migration or release completion is inferred.

Evidence: [local verification](../evidence/20261020-history-month-backend/local-verification.json),
[hosted verification](../evidence/20261020-history-month-backend/hosted-verification.json) and
[publication](../evidence/20261020-history-month-backend/publication.json).
Public [milestone](https://github.com/d00rsfan/BiglyBTC/blob/e53678361d8903f1e34b0a0c39623c78bd7dff7c/milestones/history-month-backend-20261020.json).
Recipes remain in `build/history-month-backend-20261020` and the shared documentation helper in
`build/history-thirty-next-two-20261020/finalize_docs.py`.
<!-- /p04-26-saved-history-month-completion -->
