# P04-32 — Connected Statistics controls and live history

Status: bounded connected controls package qualified; source published and
portable dist promoted. Full Statistics remains active and unfinished.
Native source: `2c5a81227e750202403fec188fbb71817aa3cfca`.
Qualification metadata: `a22006f43450fdeeca7766e2fd293b095d323d10`.
Tag: `milestone-statistics-controls-20261022`, object
`e3435675decf543689226eb13514fc6d6fbad0c6`.
Native parent: `2fbdb2bea56cb90f0af2abe2369335aa86e2db65`.
Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Dependency: `740a0b9aeabe00e762cc0efe4a0f27593db2550b`.

One related batch includes:

- Original `Stats Smoothing Secs` read, Java narrowing/clamping and actual policy
  presentation, retaining distinct raw/ten-second/smoothed measurements.
- Session-global rate and connection limits, upload-slot/choker mode and safe
  proxy family from one actual owned-session settings snapshot. These are not
  capacity estimates or per-peer routing/throughput promises.
- Successful-append receipts driving session-fenced visible History refresh,
  preserving selected dates, traffic modes, hidden/busy deferral and no polling.
- Overall/since-mark averages and original Java-style share-ratio arithmetic.
- Optional copies of actual native-written files for original-Java codec,
  Overall constructor and dated History numeric handoff checks.

Current groups put Overall/mark controls before the long operation diagnostics.
All original checks/deadlines are retained; focused new checks cover callbacks,
source policy, settings states, append uncertainty and actual single/multi owners.

Historical staged 40-file manifest:
`1193399fd77da5fc68285167f9bb067e19cc8acfb0637e916b3dd1e48511a0dd`.
Actual-settings integration amendment:
`ee5520fb946534671075c18831e2c06da92d8f5c2903c17f5164dee5b5876bc3`.
The first GCC batch found the `slots` parameter/Qt macro collision; the reviewed
name-only correction is retained with the failed log. No test or flag relaxation.

The final source input map covers 41 changed/new paths, SHA256
`e7b950fdc34ba8c11ffdee25b4956b21a567e4666fa6ef7c5eb140307054ccb1`.
Source GCC covers all 17 affected suites: 13 passes in v3, followed by the 4
previously unbuilt targets built and passed in v4. The raw v3 report retains
its 4 not-run entries; the later passes complete the same exact cohort.
Clang 17 and leak-enabled ASan/UBSan 17 pass with no failures or skips.

Actual native single/multifile outputs pass the retained original Java codec,
Overall constructor and dated History checks: 52 field comparisons, 300 history
scalar comparisons and 4 opaque-subtree comparisons. These are actual file
handoffs; no full service or availability equality is claimed.

Hosted desktop 39849224530 passes 102 units with 0 failures and 28 captures at the
exact source. SDK 39849224485 succeeds. Artifact 12456748520 has API-bound ZIP SHA
`7cd61831c7c16beb60a38d728a2d70f6688e48e57f657749e5dbb784849b0f31`.
The promoted portable tree contains 107 manifest files; 13 smoke rows and 2 actual
worker suites pass with 19 captures on private Qt and the exact uninstrumented
c35f runtime. Manifest SHA256:
`6680f0033c3b0f4f5578d93d85cd3e5279d42b1877f9802afab36b2f99773467`.
Rollback: `../BiglyBTC/build/statistics-completion-20261022/controls-publication/previous-dist`.
No system installation is performed.

Runner: `build/statistics-completion-20261022/check_controls.py`.
Logs: `../BiglyBTC/build/statistics-completion-20261022/controls-checks/`.
New evidence: [Statistics controls](../evidence/20261022-statistics-controls/README.md).
The separate P04-30/31 accounting foundation remains immutable and referenced,
not requalified or copied wholesale. All original live processes, profiles and
payloads remain untouched. Idle startup before a first transfer and remaining
original service providers stay open; this milestone does not finish Statistics.
Continue the [Statistics checklist](P04-STATISTICS-COMPLETION.md) afterward.
