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
