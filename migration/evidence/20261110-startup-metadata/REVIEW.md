# Responsive restored-library startup

A live three-second CPU stack sample identified repeated saved-torrent metadata
parsing in the session worker. Inclusive samples included 63.55% in the saved
tracker-target reader, 37.38% in regular tracker refresh and 26.17% in torrent-state
loading; these overlap and must not be summed. Whole-library periodic scans could
overrun their ten-second interval and immediately repeat. The worker then delayed
new admissions, status publication and other source-gated work. Raw stack samples
were deleted after resolving function names. No private runtime contents are
published.

Background classification now retains bounded per-row metadata results and one
immutable inventory/global-cache snapshot. File identity/size/mode/owner/link
count, nanosecond mtime/ctime, relevant saved intent/path/selection, cached
attribute sections, decoder configuration and actual admission facts invalidate
the results. Unrelated transfer counters and queue positions do not cause state
reparsing. Source snapshots are checked against fresh named-file versions and
final source/inventory validation. No profile lease, payload descriptor or
thousands of decoded torrent states are retained.

Cold work advances in chunks of at most256 rows /25ms of expensive classification
per call. Incomplete traversals publish no candidate inventory; successfully
classified rows are retained only after source validation, and the owner retries
without cancelling active requests. Network dispatch/completion and manual
command rearm always bypass the memo and authenticate the requested torrent
freshly. Periodic deadlines start after work finishes, preventing catch-up loops.
This is a cooperative budget, not a hard deadline for snapshot parsing or one
large metadata row.

The user rejected deferring tracker statistics until startup finishes. That
initial proposal was removed before delivery. The original Java comparison used
GlobalManagerImpl, StartStopRulesDefaultPlugin and TrackerStatus at the pinned
core: Waiting initialisation is independent, scraper tasks use a queued pool,
and automatic seeding ranking has bounded20/90s first-scrape waits rather than a
wait for checking to finish. Native now has no startup/checking barrier. Incomplete
and ready-seed groups are interleaved while preserving each group's force/state/
position order. Saved Force Start and the existing per-filesystem checking rules
remain authoritative. Startup intent reads no longer decode the global metadata
cache, and a processed/total indicator explains background queue restoration.

A second bug capped the entire tracker response table at256 entries, accidentally
reusing the capacity of one observer ring. The global table now follows the
already declared8192*8 bound; each ring remains256. A generated factory test with
300 existing responses verifies observer registration beyond the old limit.
This is not a claim of301 simultaneous live transfers.

The real SDK tracker-error code and bounded failure reason are now available as
an escaped tooltip in Tracker Status. The announce URL is not appended and the
details remain in RAM rather than logs. Tracker-state changes refresh the UI
without requiring a download-phase change. The field still represents the latest
alert; full multi-tracker aggregation and the Trackers tab remain later work.

Validation: four focused metadata/authority/capacity cases passed in185ms (six Qt
cases including setup/cleanup, no skips). Inotify confirms warm reads open zero
torrent states, fresh request reads bypass the memo, one-row budgets make bounded
progress, and file/permission/cache/configuration edits invalidate correctly.
Counter/position edits do not reparse state; missing/malformed files still refuse.
Generated startup cases verify the first distinct initialisations include both
an unfinished torrent and a ready seed, including a forced-seed prefix. UI,
chat-publication and final Start/Stop/close tests passed.

Six owned end-to-end data rows exercised actual IPv4/IPv6 Mainline discovery,
HTTP/UDP trackers, payload exchange, filename variants and joined reopen. The
aggregate180s CTest limit expired after five passing rows while the sixth was
running. Only that remaining IPv6 row was rerun and passed in30.062s. The future
aggregate limit is1000s. Initial compile/test-fixture failures remain in local
logs; no product assertion failure is being hidden. The timeout's orphaned
generated directory was removed and active test directories were preserved.
No fresh sanitizer result is claimed.

The frozen chat-preview executable was atomically updated, reusing the existing
Qt/provider. The running user client and protected daily-use candidate were not
stopped or changed. Real-profile performance, rapid Checking and concurrent
seeding acceptance remain pending user restart. Existing unknown piece maps
remain unknown; no old backup was used to invent completion. This is not a claim
to fix all earlier Native-induced resume invalidation or complete migration.
Full upstream/native pins, image and log hashes are in RESULTS.json.
