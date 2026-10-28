# P04-13 Persist native classic choices without changing original Java layouts

State: bounded native preference slice implemented, qualified, committed and
pushed in `ba03f2c6a080f856c0d8808b2b4239846dd27733`.
GCC42 units pass with no skips. Clang27 codec cases/331 assertions and43
storage cases/5823 assertions pass. Seven leak-enabled ASan/UBSan suites pass;
new Qt header/window-worker suites have27/12 actual rows. Actual preview smoke
passes4 Qt rows at each100/150/200% scale. Public hosted desktop run39130607531
independently repeats42 units,39 new Qt rows and4 smoke rows; SDK run39130607432
also passes. This is native-only preference persistence, not full Java layout or
client/plugin completion.

The user’s “do next” authorizes this next bounded implementation. Root accepted
proposal B under that existing task scope: explicit native-only preferences under
`BiglyBTC.ClassicColumns.v1` in existing `tables.config`; preserve every original
Table./TableDefault./legacy/plugin record. Original Java table reset-all erases the
whole root including the private map; ordinary pinned map saves retain it
semantically, with no raw byte-order equality after Java re-encoding.

Strict version1 stores both panes and13 stable native IDs: visibility, requested
width16..8192 except responsive Name, complete native order, and one native sort.
Unknown private fields/versions refuse overwrite. Whole/private bounds are
4MiB/100000nodes/depth64 and32KiB/1024nodes/depth8. Valid native preferences win
on startup/explicit Reload; absent namespace retains original read-only fallback.
A failed optional preferences read leaves library opening available.

Only explicit View > Save current column layout writes. Worker owns the held
lease/session and last successful pristine read baseline. Existing unchanged
Primary bytes are required; Missing/Backup/Saving creation/promotion is excluded.
Existing ConfigFile byte/presence conflict and .bak/.saving transaction guarantees
remain unchanged. No primary-inode CAS or external noncooperating-writer race
solution is claimed. Once synchronous save starts, its actual result is returned
regardless of later cancellation; post-rename errors can mean replacement happened.

Capture and acknowledgement do not import columns, refresh/revise library rows or
retarget filter/selection/details. Newer local revisions remain unsaved. Reset
keeps construction defaults and writes only when explicitly saved afterward.
A reviewed rejected-dispatch pending-queue stall was corrected with a focused
real-Qt regression before final qualification.

Three generated private layouts survived actual source-built Java BDecoder /
BEncoder map update/re-encoding and native decoding. This helper disables key
interning to avoid StringInterner’s timer. Frozen original classes, process fence
and class-initialization logs are retained. No manager/FileUtil/config/timer/client
initialization occurred; no full Java manager/save/reset runtime parity is claimed.

Proposal input pins remain immutable at
migration/evidence/20261018-classic-layout-persistence-next. New reviews, Java
codec receipts and pre-qualification native hashes are under
migration/evidence/20261018-classic-layout-persistence. Current code is in the
durable sibling ../BiglyBTC/preview/native, not /tmp. All21 committed source blobs match qualified hashes. Receipts include
local-verification.json, hosted-verification.json, source-commit.json and
dist-promotion.json. The portable ../BiglyBTC/dist preview is promoted,103 file
hashes verify and82 runtime files are unchanged; previous-dist remains as rollback.
Initial staging stopped on an overly strict raw build/installed ELF comparison.
The corrected guard replays the exact CMake RUNPATH change and checks complete
byte equality; source/test flags and checks are unchanged. No system install.

Full registered/plugin-aware shared Java layout writing, missing-file creation,
normal-profile switching and complete client/plugin migration remain open. No
user question blocks this slice. The running original Java/profile/payload remain
outside all development tests and are never signaled or changed.

Public milestone metadata: `2501968f62d4ec7e70b093c2186a31fcf2ede518`; tag
`milestone-classic-layout-persistence-20261018`. Publication receipt is publication.json.
