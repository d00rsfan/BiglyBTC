# P04-05 Saved hosting/sharing projections and views

State: verified bounded readers/models/worker/tab integration is committed/pushed
as `0b70eb6b0565f2a1debf2e7cd321e9bcb17b31b6`, with public milestone
`2c309fe11dcecd4606bd848c9f780fd062391767`. All30 local/hosted units pass, including
17 Bridge and19 navigation Qt rows; both new suites pass leak-enabled ASan/UBSan.
Three-view100/150/200% smoke passes. Full tracker/sharing services and broader
profile/performance parity remain OPEN. Later saved queue-position work is P04-06.

Original baseline: BiglySoftware/BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; exact23-file audit and native
policy boundaries are in
`migration/evidence/20261018-navigation/persisted-hosting-sharing-audit.json`.

First deliverable is a pure owned read-only projection from supplied parsed
`tracker.config` and `sharing.config` documents. It performs no file/clock/path
resolution, manager initialization, scanning, torrent creation or network work.
Retain unknown data and source list indexes. Tracker persistent=false entries
are statistics backups, not active hosted rows. Share ihash is a filesystem
fingerprint, not a torrent infohash; ifile/per_key/path values remain inert.
Dates, live peers/names/health and tag display names are not fabricated.

The initial core API is `saved_services.hpp` in the durable public sibling's
`preview/native/core`. Return a privately owned immutable document, typed optional
fields and bounded fixed-code diagnostics. Document native caps/policy differences
explicitly. Existing parser rejection of duplicate dictionary keys remains a
compatibility limit; duplicate list records must remain represented. Reject
edited documents whose raw backing would disagree with the projection.

Independent generated-fixture tests and source review precede actual compilation.
These first fixtures are source-derived, not Java-runtime equality evidence.
Root owns CMake integration/builds and all future profile-owner/UI integration.
No uncommitted source is a verified component. Future reader/view work must
distinguish missing/invalid/empty configs, source recovery choice and offline
persisted state without activating services. Cancellation, target generations,
page teardown and immutable profile bytes require integration tests before that
later slice can be called implemented.

## Actual progress and next integration

Published native source commits:

- Readers: `3d4f86d3f891675238be05ebbad8202c99cd6817`.
- Standalone pages: `d5ba39d326b3f08d9f77c00f4446ecf3102a5f83`.

The actual verification record and downloaded hosted evidence are in
`migration/evidence/20261018-saved-services/verification.json`. These are bounded
synthetic, source-derived checks; they do not qualify Java manager/runtime equality
or operational service behavior. Standalone pages implement request-tagged
NotLoaded/Loading/Missing/Ready/Failed states and provenance. All labels are plain
text; personal keys/cache references are not displayed. Missing lists and empty
verified lists remain distinct.

Bridge work in progress reads both fixed files on the current worker/lease and
returns independently typed results. The new suite passed17 actual Qt rows,
including direct receiver deletion and cancellation. An independent review found
an inherited callback lifetime flaw; the applied QPointer guards and regression
rows are part of the upcoming integration commit, not either published commit.

Bounded integration contract (verified above; full services remain separate):

1. Add a UI-owned immutable result bundle with a window-global request ID and
   separate hosting/sharing success/missing/failure results. Keep Qt out of core.
2. SessionBridge reads both configs on its existing lease/worker, bounded and
   cancelable, without acquiring another lease or launching services. One bad
   config must not turn the other into a false empty success. A canceled result
   is discarded. Deliver successful service results before busy=false so the
   window can distinguish completion from cancellation/queued rejection.
3. Add stable My Tracker/My Shares main pages and real Ctrl+2/3 navigation only
   after the reader/page tests pass. My Torrents remains permanent; auxiliary
   tabs close and reopen by stable identity. New/reopened pages use global
   generations so a late reply cannot revive or retarget a closed page.
4. Serialize pending service reads behind existing work and join an existing
   request only while the same generation remains current. New/reopened pages use
   a newer generation and replace the latest pending request. Prefer queued GUI dispatch after current
   completion; avoid nested dispatch during busy-state notifications. On rejected
   or canceled work, finish the page state honestly rather than leaving Loading.
5. Torrent-specific actions must have no target while an auxiliary page is active.
   Retain torrent selection/data, suppress automatic hidden-table detail work,
   restore focus/actions on Ctrl+1, and route Refresh to the active view's data.
   Never let Ctrl+R/Ctrl+I operate on a hidden prior torrent from a service page.
6. Extend explicit generated preview input configs and smoke checks, preserving
   every config/cache/state/payload byte and no networking. Use only labeled
   synthetic saved rows. Saved Started is not proof that a tracker is running;
   sharing fingerprints are not torrent identities. Do not read cached torrent
   paths or resolve metadata merely to make a full-looking table.
7. Add real shortcut/tab/lifecycle/async tests, independent source review and
   actual offline build/smoke evidence before committing/pushing the next source
   milestone. Full operational services and profile/performance parity remain open.


## Verified bounded integration milestone

Native source `0b70eb6b0565f2a1debf2e7cd321e9bcb17b31b6` is committed/pushed.
My Tracker/Ctrl+2 and My Shares/Ctrl+3 now show closable saved-data tabs. My Torrents
is permanent; Ctrl+1 preserves focus/selection. Torrent-specific actions are
unavailable from auxiliary tabs. Current worker/lease reads are serialized with
one active/latest pending request, stale generations rejected on close/reopen,
and cancellation/failure states completed honestly. No service is activated.

Actual local validation:30/30 units,17 Bridge Qt rows,19 new navigation rows,
20 standalone page rows and3-row full preview smoke at100/150/200%. Both Bridge
and navigation suites pass Debug ASan/UBSan with leak detection. Independent
source reviews pass the Window/Bridge/integration contracts. Actual source hashes,
reports and screenshots are pinned in the integrated verification record. Hosted
SDK passed; hosted desktop is currently running. No Java manager/runtime parity,
real compositor, full service activation, seamless profile switching or measured
performance claim follows from this milestone.

The runnable dist is promoted with103 verified files;82 dependency/runtime files
remain byte-identical to the predecessor. The full old dist is preserved. No
process was signaled and no system package was installed. Visual followup: initial
saved-service column widths could better fit descriptive Record/Type labels and
long byte-counter headers; users can resize them, and automatic width policy can
be improved as a separate small UI change.


Hosted saved-view desktop run39114203091 completed successfully:30 native units
and the real generated-profile three-view smoke/captures pass. SDK run39114203106
also passed. Public milestone commit `2c309fe11dcecd4606bd848c9f780fd062391767`
is tagged `milestone-classic-saved-views-20261018`; its source is the separately
pinned `0b70eb6b0565f2a1debf2e7cd321e9bcb17b31b6`. Downloaded hosted reports are
retained with the integrated verification evidence. Earlier “running” statements
above record the previous observation and are superseded by this result.
