# P04-17 Measured engine traffic graphs

State: implemented — bounded measured-engine traffic graphs qualified and published.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
native parent `30569e2631427003a4a92e7edee3572a3b34e740`.

Add Activity > Engine traffic beside the retained default Payload view. Display
actual combined/payload/protocol receive and send interval rates from P04-16
immutable owned-engine packets. Keep the two existing top-level Statistics tabs,
saved totals, payload view, shortcuts and My Torrents state intact.

Window owns at most600 actual known-rate observations independently of Page
lifetime. Plot against owned steady elapsed observation timestamps, not GUI
arrival time or invented periodic slots. First/unavailable rates add no artificial
zero. Actual known zero is valid. Break lines across skipped sequence or unknown
rate observations; never interpolate these gaps. A terminal duplicate sample is
not another observation. A genuinely later terminal sample may stand alone.
Keep last actual measurements after success/cancel/failure without exhaustive
final-counter or current-after-shutdown claims. New operations/profile sessions
clear the series. Saved refresh and Ctrl+1 preserve it. Replay immutable bounded
snapshots after opening/reopening Statistics, including transfers while closed.

Opening/painting/refreshing the tab creates no engine, sampling thread, timer,
network activity or statistics write. Hidden/minimized views cache bounded data
without painting or label updates; show/subtab/restore renders latest facts.
Use plain-text bounded names and exact bytes/s labels, explicit one-operation
scope, safe INT64 plotting geometry and Qt lifetime/reentry guards.

This is a scoped native policy, not original global Activity, Java ten-second
smoothing, application session/lifetime, limits/swarm or persistent history parity.
No P03 integration or full client/performance gate advances.

Qualification: history ordering/identity/reset/eviction/unknown/zero/gap/terminal
and immutable ownership; chart geometry and Qt hidden/minimized/deletion/reentry;
real generated single/multifile loopback transfer graphs cached across closed and
reopened tabs, cancellation/teardown and fresh recheck. Preserve all existing50
unit suites, Page92 and engine12 Qt rows and their assertions/flags/deadlines.
Use GCC16/Clang24 C++23/Qt6.12.0, leak-enabled ASan/UBSan, actual three-scale
offline/engine captures and standard hosted CI. Freeze exact upstream/native hashes,
verify installed-RUNPATH bytes and103-file portable dist with82 private runtimes,
retain the whole previous dist, commit/push/tag, validate registry and preserve
original HEAD/index in a separate private review checkpoint.

Root owns Window/CMake/actual transfer tests/publication/evidence. Parallel agents
own bounded history, graph/Page UI and independent source audit/review. Drafts and
builds remain in persistent `build/engine-activity-20261018` in each repository.
Generated profiles only use fresh `/tmp/biglybtc-*` paths and owned loopback ports.
Never access or interrupt the running Java client, original live profile/payload,
public peers, discovery, trackers, DHT or plugin runtime. No blocking question.

<!-- p04-17-engine-activity-completion -->
## Completion evidence

Source `a84e4ecc6f78112c115c182aefc13bc4a1f3ac17`, metadata `9d6023fca3b322dfbe5d92d1666b7f2120cd7752`,
annotated tag `milestone-engine-activity-20261019`. All 17 changed/new source files match tested Git blobs.
GCC 53 unit suites, Clang 5 focused suites and 9 leak-enabled ASan/UBSan suites
pass without skips; all 50 earlier units remain in the GCC cohort. Existing Page
92 and owned-engine 12 Qt rows remain intact. New History: 52 Qt rows; View/Page:
26 Qt rows; real graph Window/worker: 7 Qt rows. At each 100/150/200% scale,
six offline smoke rows, four real engine-counter rows and four actual graph rows
pass, producing six offline + two counter + two graph PNGs (30 across three scales).
Hosted desktop 39167136970 and SDK 39167136902 pass on the exact source; the inspected
artifact has 53 units and ten captures. Dist 103 hashes verify,82 private runtime
files are unchanged and the previous complete dist is retained.
No original profile/payload access or process signals; original HEAD/index unchanged.

These are actual interval-rate observations from one experimental engine
operation/session, not original global Activity or application-wide/session/lifetime
history parity. Window retains at most600 known-rate points; unknown/skipped-sequence
intervals remain disconnected, known zero is real, repeated terminal samples are
deduplicated and a later actual terminal point is standalone. Ended is last observed,
not exhaustive final accounting. No Java ten-second/eleven-entry smoothing, IP/
tracker/DHT, persistent history, statistics writes/reset/mark, cross-run accumulation,
persistent-owner/full-client/plugin, seamless-switching or performance claim.

[Local verification](../evidence/20261018-engine-activity/local-verification.json),
[hosted verification](../evidence/20261018-engine-activity/hosted-verification.json),
[publication](../evidence/20261018-engine-activity/publication.json), source-commit,
dist/RUNPATH receipts, exact test sets, source reviews and recipes are retained.
[Public milestone](https://github.com/d00rsfan/BiglyBTC/blob/9d6023fca3b322dfbe5d92d1666b7f2120cd7752/milestones/engine-activity-20261019.json).
Complete rollback: `~/Java/BiglyBTC/build/engine-activity-20261018/previous-dist`. Earlier P04-16 and P04-15 evidence/rollback are unchanged.
The one requested P04-17 milestone is complete; report the result to the user.
Do not automatically start a further milestone.
<!-- /p04-17-engine-activity-completion -->
