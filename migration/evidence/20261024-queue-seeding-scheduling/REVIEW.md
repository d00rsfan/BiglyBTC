# Queue, seeding and Speed Scheduler: qualified native preview

Native source `cda09f95bed76f3c29a514b345a70a29881dfc85` is published directly on BiglyBTC main. Original
BiglyBT baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; SpeedScheduler
baseline `1ee6a53dae017696d2f0002e4a0d74f5a9476af5`; vendored native plugin
`91c417ca5aa5494f8c5791d8f29cf96350a0071d`. This closes the bounded P08-01
preview chapter. The broader component records remain in progress where the
original classes require additional providers or recovery behavior.

## Resulting behavior

The normal worker now owns queue decisions. It verifies handles under
stop_when_ready, then applies queue pauses before new starts, with LT automatic
management disabled for these admissions. Download/active limits, minimum
downloads, ordinary seed ranking, first-priority All/Any criteria, ignore rules,
minimum seed time, timed rotation and activity hysteresis use original settings.
Forced downloads bypass slots; active forced seeds count. Explicit manual
pause/stop/force intent takes priority. Unknown tracker/time/rate observations
remain unknown; they are not supplied as zero counts. The classic library shows
queue reasons and first-priority state without refreshing solely because an
undisplayed numeric timed rank changed.

Original cumulative counters and downloading/seeding seconds survive restarts.
New elapsed time comes from actual exact-handle state/pause/removal events;
verification does not count as downloading. Lost timing events refuse a guessed
checkpoint. Completion clears force through a narrow original-config transaction
when configured, preserving current pause/startup/order and opaque values.
Admission capacity of 8,192 is checked before storage preparation. Queue planning
uses one bounded sort and indexed effects; timed alternatives use a linear
prepass rather than scanning all candidates per expired seed. No comparable
whole-client CPU/memory performance claim has been made.

Tools → Options now represents 100 original fields on 9 pages, including Queue →
Seeding → First Priority/Ignore. Fifty added fields include 42 queue settings and
8 dormant source controls. Original-only features remain labelled. The classic
ui=az2 first-priority default and binary32 Stop Ratio representation are retained.
Saves remain sparse and revision/CAS protected; untouched bad/unknown values
survive. Numeric mode fields describe their original values in tooltips; broader
original conditional editors are still follow-up work.

Tools → Speed Scheduler opens the new editor. It supports day/time/rate and
pause rules, category/manual-tag filters, forced override, default speeds,
enabling and polling. Original local-time closed endpoints and numeric maximum
overlap behavior are retained. Schedule evaluation calls the actual native
plugin ABI. The app authenticates the bundled module's exact owned descriptor,
size, SHA and version; profile JARs are neither loaded nor modified. The approved
module is distributed in app-owned dist/lib; profile scheduling data remains in
the original plugins/<actual directory basename> directory and config namespace.
Full profile-local native plugin installation/updating remains CF-07.

SavedSchedules.xml is patched sparsely, preserving unknown XML/comments and
UTF16 where supported. Opening missing data does not create files. Original
PausedTorrents.conf formatting and unrelated lines survive. Durable intent
precedes pause/resume effects; only acknowledged current-session actions grant
resume authority. Manual actions revoke it before control. Inherited legacy
pause hashes never grant automatic resume authority. An interrupted uncertain
action remains blocked until an explicit manual override/review; automatic
recovery and its editor are still follow-up work. XML and config are separate
transactions: partial save results explicitly retain unsaved preferences.
Scheduled rate writes reload config CAS before creating subsequent plans, so a
rate change does not invalidate the same tick's pause or the next user edit.

## Execution evidence

Primary GCC 16 covers 23 unique related suites: 18/23 initial pass, five repaired
suites then pass, and the final Qt suite passes after a debugger-confirmed test
lifecycle correction. Final Clang 24 and leak-enabled ASan/UBSan each pass 23/23
at identical final source inputs. Once Clang released its two slots, the sanitizer
build was interrupted before tests and resumed from the unchanged cache with 3
jobs; no already-passing tests were repeated. That build-only interruption is
retained without execution credit. The existing counter-shim vptr-only exception
remains explicit; the approved torrent provider is not claimed fully sanitized.
All complete logs, source maps, command receipts and initial failures remain in
this evidence directory; passing unrelated checks were not repeatedly rerun.

Actual retained original Java rank/FP methods provide 190 admitted scalar
comparisons, all matching native. Unknown scrape is characterized separately,
not credited as a match. Initial JVM bootstrap failures and strict-fence provider
amendments are retained. Final native replay reuses the exact frozen original
observations rather than repeating Java. This establishes scalar rank/FP
behavior, not equality of the entire Java queue scheduler.

Actual original COConfigurationManager passes Java → native → Java → native
queue settings, including decimal Float/string values, conditional missing
keys, Unicode/opaque subtrees and unchanged unrelated fields. The 5,432 retained
original classes remain hash-identical; only tiny helpers compile. The final CMake-built native driver also reloads the
retained Java-resaved config; its linked archives match the four-phase oracle.
No original
core rebuild/startup, user profile or running client was used.

Real source-bound tests verify two competing downloads, manual/force precedence,
completion clearing, preserved saved baselines, pause/restart/checkpoint and
actual loopback payload transfer. Host tests load the actual approved module and
verify actual 7/9→17/19KiB/s limits, scheduled pause, subsequent editable revision,
manual Stop, disable restoring defaults, legacy non-resume and joined closure.

Hosted exact-source desktop 155 and SDK 2 pass;
standard public GitHub runners are used. Private Qt 6.12.0 with the approved
source LT passes 13 smoke rows and 11 affected suites. The staged module matches
the compiled application approval byte-for-byte. Seven current settings/Scheduler
images and 28 existing classic/Statistics images are retained; these are offscreen
checks, not compositor/scaling or public-swarm qualification.

Two initial runtime defects were fixed: owned0775 plugin data directories were
incorrectly rejected (world-write rejection and executable authentication remain),
and guarded GUI submission needed retained row-control identities. An existing
seeding test raced a disconnect after a successful complete transfer; actual
owned-leecher connection alerts now retain evidence. The final GUI test initially
removed a Qt-owned cell without unregistering it: gdb located the stale editor
in Qt teardown. It now calls removeCellWidget before actual deletion; all dirty,
no-save and unavailable-control assertions remain. No deadline/assertion was
removed to obtain a pass.

Portable dist is promoted with 118 files; manifest
`0cb16425cde2f4782928a87544742445c8b734177bf0db97ec6b9a38f00f604d`. Complete previous dist:
`/home/yurii/Java/BiglyBTC/build/queue-20261024/portable/queue-final/previous-dist`. There is no system installation or DEB.
The original running Java client, ~/.biglybt, real payloads and ordinary original
HEAD/index were untouched.

## Remaining compatibility

CF-04 retains lightweight/reserved/flexible seeding; active speed/ETA probes;
external/tag-specific first priority and limits; real disk-latency, smoothed-rate,
stale-rank and file-priority providers; saved new-seed/group position normalization;
and original mutable within-cycle bandwidth details. Conservative unavailable
facts are visible diagnostics, not full original scheduling parity.

Exact last-announce/scrape fallback and self exclusion, original lifetime rounding
at pause boundaries and extreme Java share-ratio overflow projection need further
qualification. Inherited Scheduler ownership/interrupted-action recovery needs a
separate supported policy/editor. Broader original Options, conflicting-draft
merging and recovery editing remain open. CF-01–CF-08 retain storage layouts,
v2/hybrid, additional network/proxy routes, file operations, other plugins and
measured runtime/performance qualification. No blocking human questions remain.
