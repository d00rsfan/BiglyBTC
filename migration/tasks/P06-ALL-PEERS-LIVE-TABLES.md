# All Peers and passive torrent table updates

Authorised 2026-11-12 after the lazy queued verification candidate. The user
observed actual upload in Statistics; missing torrent-table Up speed is a
presentation defect, not proof of absent payload. Include the subsequent reports
of horizontal scroll resetting and insufficient Tracker Status in this milestone.
Do not interrupt the running preview or open the real profile concurrently.

Provide View > All Peers from actual retained engine connections, including their
torrent, endpoints/interface, payload rates/counters, client, state, transport,
availability and optional local metadata. Read original Table.AllPeers layout
without rewriting it. Inspect on the existing worker only while the tab is
visible; coalesce delivery and reject stale session/sequence results. No new
tracker/DHT discovery or peer-IP egress for metadata. Restore original saved
visibility/order/width/sort; header edits are session-only in this candidate.

Preserve observed per-torrent rates in complete library snapshots. Preserve both
Seed and Leech viewport positions across passive snapshots/telemetry/details.
Match Java TrackerStatusItem/DownloadManagerImpl's announce-or-scrape source:
show actual reason in the cell, bounded full detail in an escaped tooltip,
known returned peer counts and honest queued/checking/no-result states. Failures
on another tracker/address family must not erase a working announce result.
Do not generate full library refreshes merely for a countdown.

Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
PeersSuperView, PeersViewBase, TrackerStatusItem, TrackerCellUtils and
DownloadManagerImpl.getTrackerStatus/getTrackerClient. SDK unchanged from the
lazy queued resume candidate. Qualify synthetic UI layout/filter/sort/lifecycle,
rates and viewport regressions, actual owned tracker reply counts, and one owned
ordinary IPv4 tracker/DHT/payload cold/warm control with actual peer snapshots.
Keep full build logs and real-profile diagnostics local. Commit/push main;
prepare preview, request ordinary Native Exit before replacing it, and let the
user run it. No automatic real-profile launch or next chapter.

Public review: ../evidence/20261112-all-peers-live-tables/REVIEW.md.

## Next required compatibility investigation (not started here)

The private post-Exit delta has missing pieces changing to RECHECK without
loss of DONE pieces or transfer/completion counters. Distinguish incomplete
preparation after durable map staging from early retirement with invalidated
unchanged-payload proofs. Reproduce with generated partial and entirely missing
payload, including a normal joined close and preparation failure. Do not infer
that preceding lazy queue work fixed every resume transition; do not restore
old source maps into the live profile. Keep captures and identifiers private.

## Authorised UX follow-up: offscreen selection and open context menu

The user reported the offscreen current row briefly appearing on each passive
update, then disappearing when the earlier viewport guard restored scrolling.
Observe intermediate scroll/paint exposure, not only the final scrollbar value.
Retain unchanged current/selection, suppress automatic reveal during model
updates and restore the viewport before reenabling painting. Cover Leech/Seed
above and below the viewport, telemetry/details/snapshot/reset, All Peers with
multiple selected connections, and normal user keyboard navigation afterwards.

A second user report: upper context-menu commands turn grey after the next
library snapshot. Ordinary status/rate revision changes must not invalidate an
open menu targeting the same torrent identities in the same profile session.
Recompute current command eligibility and submit current model generations.
Preserve refusal after selection/profile changes, target removal, permission
revocation, stale rendering and reentrant dispatch. Keep the broad revision
fence for global Start All/Stop All operations. Never freeze background library
updates while a menu is open.

This follow-up changes UI only. Keep the running client's torrent testing
uninterrupted. Build/test and commit/push main; leave the frozen preview intact
until a later coordinated ordinary Exit. No next chapter or live resume repair
is included. Record immutable implementation and results in
../evidence/20261112-passive-table-selection/REVIEW.md.
