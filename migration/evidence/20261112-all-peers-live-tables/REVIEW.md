# All Peers and passive torrent table updates

Implementation c5bc5faf67c0b053ac30b71f25ebe03339c57ca4, tree de984d5686f1cb278fcc819eb9a6bf2be168a66f; native source subtree
0e5e9b0abe2bab032b1c8e09ea8961d4b7ad37e2. Original Java cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
PeersSuperView/PeersViewBase, TrackerStatusItem/TrackerCellUtils and
DownloadManagerImpl.getTrackerStatus/getTrackerClient. No upstream update.
SDK unchanged: upstream740a0b9aeabe00e762cc0efe4a0f27593db2550b,
overlay parent4ff478031095830fcfb7531dd6bc0f21b30906af, shared object SHA256
8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3.

## Behaviour

View > All Peers displays actual connections from retained SDK torrent handles,
including torrent/interface/endpoints, client, direction, transport, availability,
payload rates/counters and connection/choke/interest/request state. Same-endpoint
peers on different torrents remain separate. Numeric address/rate sorting,
filtering, column selection, copy address/endpoint and opening torrent details
are provided. Original Table.AllPeers visibility/order/width/sort is read using
the existing validated column-layout parser; the temporary namespace wrapper is
never persisted. Names come from the existing library snapshot.

Inspection runs on the existing worker only while the tab is current, visible
and not minimised, at most every two seconds. It reuses normal peer-count samples
to avoid empty per-handle SDK RPCs, bounds rows at20000, coalesces deliveries and
rejects stale profile/sequence packets. No new alert consumer, discovery,
network/accounting ownership or profile writer. Hidden views stop requesting.
Optional country data comes from an authenticated local provider; AS comes from
the original saved local prefix cache. No new peer-IP DNS/HTTP request is made.

Complete library snapshots previously rebuilt rows without their observed
rates, while frequent startup/tracker updates prevented the telemetry-only
branch. Global upload could work while Up speed stayed unavailable. Snapshots
now carry observed per-torrent up/down rates, including I2P provider rates when
available; stopped/unavailable rows remain unknown rather than invented zero.

A generated UI regression reproduced Seed horizontal scroll moving from200 to0
on passive refresh. Both torrent viewports now survive complete snapshots,
telemetry and details updates; peer snapshots preserve their viewport too.
The peer view uses the existing ClassicSortHeader direction convention.

Java displays the first line of the actual announcer status, falling back to
scrape if there is no announcer, with detail in the hover tooltip. Native now
shows actual failure reason, known returned peer count on success, active
announcing and honest queue/checking/no-result states. Existing scrape results
and counts are used when no announce result is available. Full bounded error
text is HTML-escaped in the existing tooltip. HTTP diagnostic bodies stay out
of the cell; no countdown causes periodic full-library refreshes. Failure on a
backup tracker or another address family does not erase a working reply.

## Qualification

Linux amd64, Qt6.12.0, C++23, existing Release build; no new sanitiser or hosted
CI claim. Compiler logs and full fixture traces remain local.

- BiglyBTC and three affected test executables built successfully.
- AllPeersUiTests:7 QTest passes, covering original column layout, numeric sort,
  filter, duplicate endpoint/torrent separation, selected peer, stale sequence
  and session, full-snapshot rates, tracker reasons/scrape/escaped tooltip,
  both torrent scrollbars through repeated snapshot reorder/telemetry, and
  classic action/visible-view subscription.
- ManagedTrackerTests scoped final run:4 QTest passes. Route/URL isolation and
  actual owned HTTP tracker/peer transfer; the actual reply supplies one peer.
- OrdinaryDailyHostTests IPv4-cold-warm:3 QTest passes, approximately30s. Real
  owned Mainline/HTTP/UDP discovery, payload transfer, joined retirement/warm
  resume, peer packets with nonzero payload sent/received and matching torrent
  identities, and full library snapshots containing actual rate observations.
- Initial network fixtures run inside the socket-restricted sandbox failed or
  timed out. The final scoped network runs above passed with local socket
  access. This is not a pass of the entire managed-tracker or six-row discovery
  matrix. UI checks passed without network access.

## Limits and preview acceptance

This is a scoped live table implementation, not complete original peer tools.
Peer header edits are session-only; saved original layout is restored read-only.
Country/AS may be unknown when local metadata is unavailable. External per-peer
ASN lookup was rejected by automatic approval review; the implementation uses
local data instead, and does not route around that restriction.

User confirmed existing upload in Statistics and exited Native normally before
preview replacement. The compact private metadata comparison found a remaining
partial-map compatibility defect: some missing-piece flags became RECHECK while
DONE pieces and historical transfer/completion counters stayed intact. This
milestone does not repair or declare acceptance of the preceding resume owner.
Investigate the original-library preparation/retirement boundaries next, using
generated fixtures and the private delta. Do not blindly restore historical
maps, publish profile captures or open real payload for diagnosis. UI fixes and
the read-only peer observer do not change those write paths.

Replace the frozen preview only after confirmed Exit; do not retain a rollback
application image or launch the real profile automatically. The user must check
real scroll/rate/tracker behaviour after restart. Do not start another chapter.

The new component record passes the registry schema. Whole-registry validation
remains blocked by pre-existing record/schema mismatches in earlier components;
those unrelated historical records were not changed in this milestone.

The frozen preview was replaced after confirmed Native Exit and successful
profile/runtime lock acquisition. Executable SHA256 c938a87510c84e0cc50d1b4669fa6b65436b5c83c83e1cb1e393b7063fd3a6c9,
19277696 bytes. The agent did not launch it, write the profile or keep
a rollback executable. User observation of this image remains pending.
