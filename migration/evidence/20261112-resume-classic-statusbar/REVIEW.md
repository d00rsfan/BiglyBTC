# Mixed resume preservation and classic status strip

Both requested bounded stages are implemented and qualified. The local frozen
preview is updated; real-profile and visual acceptance remain pending.

Resume implementation 58802baa363a50f89da11e4fb1ae4200f8211916, tree 2ec32f1731c63e942e681b857297c30fb430f9e7.
Status-strip implementation f1fbd867f4b23104699908f5ac04a62c01740c50, tree 4f89c6f5587280dd55ebc18936873f27eef53d2e.
Original BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
RDResumeHandler.checkAllPieces/saveResumeData, MainStatusBar and
UIConfigDefaultsSWT. SDK unchanged: upstream740a0b9aeabe00e762cc0efe4a0f27593db2550b,
overlay4ff478031095830fcfb7531dd6bc0f21b30906af, shared-object SHA256
8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3.

## Pending resume metadata

Java preserves DONE independently of pending/started pieces. Native previously
rejected the entire mixed map for SDK import and then staged all pieces as
RECHECK. Even an admission held in Queued could erase its previously saved DONE
bits before any actual check. The user explicitly reported waiting for visible
checks to finish before normal Exit; do not attribute the observed discrepancy
to manually interrupting active checks. Queued unclassified admissions are a
separate lifecycle state.

A well-shaped own-admission input map is now retained separately from SDK
resume authority. Staging preserves its DONE and existing pending/partial-block
information, marking selected missing pieces conservatively for possible native
writes. No mixed map is imported into SDK.have_pieces. After joined retirement,
only an owner-approved unclassified, zero-download admission with fresh per-file
proofs restores that exact input resume/valid flag/blocks. Rejection, hash
failure, mutation or missing proof cannot certify old bits. Saved progress is
retained while unclassified; actual completed verification replaces it normally.
No historical private snapshot is restored to the real profile.

This prevents expansion during queued normal restarts. Actual selection/Force
Start still performs real SDK checking; selective hashing of only the uncertain
pieces in a mixed map is not implemented here. Previously lost historical DONE
information is not reconstructed blindly. Crash checkpoints remain conservative.

## Classic lower strip

The strip shows BiglyBTC/version, Mainline status and actual service counters,
existing Azureus DHT/chat unread controls, overall ratio, current public network
information, incoming-connection observation, download/upload rates and effective
limits. Original Status Area Show SR/NAT/DDB/IPF/NI flags/defaults and rate
formatting options are read without changing configuration. Hidden, narrow and
fresh-profile states retain honest unavailable values and reject stale packets.
Transfer information updates through the existing independent session stream
with Statistics closed. Buttons navigate to existing transfer/counter details.
The new presentation adds no poller, resolver, network permission, alert consumer,
profile writer or repeated library query. It uses the existing worker's passive
Mainline snapshot and already-collected runtime metrics.

An incoming connection observation is not an active port test. Mainline counters
are not an estimated DHT network size. Original IP-filter range/blocked/banned/
bad-IP counts have no full Native service yet: display an unavailable value,
with actual SDK banned-disconnect events described separately in the tooltip.
The user explicitly leaves IP-filter low priority. Speed sliders/filter controls
and a dedicated Mainline network-size view remain outside this display slice.

[Synthetic layout capture](statusbar-generated.png) uses generated torrent names,
example network data and injected observations, not a real library or network
acceptance screenshot. Visual inspection found a misplaced Azureus indicator;
its order was corrected and is now asserted. Source rate/protocol values are
kept distinct in tooltips; byte/bit units and actual applied limits are preserved.

## Qualification

Linux amd64, Qt6.12.0, C++23, existing Release build. No sanitiser/hosted CI or
whole-client parity claim. Full compiler/test output stays local.

- Existing check-owner suite plus four new linked/linear mixed-map, started-block
  and mutation controls:34 QTest passes,18.95s. Covers actual scheduling limits,
  clean partial/V1/V2/mapped restore and retirement guards already in that suite.
- Added actual queued mixed-map two-close/reopen followed by real Force Start:
  3 QTest passes,624ms. Zero hash operations while held; exact resume survives
  both exits. Force Start actually hashes and saves the resulting DONE map.
  This is a separate scoped run, not a second full owner-suite invocation.
- Final status/table UI:12 QTest passes, including configuration/unknown values,
  live rates/limits with Statistics closed, network/ratio/NAT/DHT fields, stale
  profile rejection, placement, navigation and prior selection/viewport controls.
- Final context/action UI:31 QTest passes.
- Actual owned ordinary IPv4 Mainline/HTTP/UDP/payload cold/warm Host:3 QTest
  passes,30.78s, on the final engine/Host implementation. Only indicator placement,
  accessibility text and the synthetic screenshot fixture changed afterwards;
  that green network run was not repeated for a layout-only change.
- Application and affected test targets built successfully. Existing unrelated
  GCC CategoryCommit maybe-uninitialized warning was recorded locally, not
  investigated or advertised as fixed in this milestone.

Frozen executable SHA256 e97c56d033d4e80f741b076315aaae41208d1ad3d63c79eba0b7219564333113,
19328904 bytes. Replacement held the Native runtime lock;
it neither opened the original profile nor interrupted Java. No rollback image
or automatic app launch. Java must exit normally before the user starts Native.

## Next proposed chapter

Complete the torrent-property tabs and their missing dependencies, following
MyTorrentsView's fifteen core registrations and reviewing plugin additions.
[Dependency proposal](../../tasks/P06-TORRENT-DETAILS-PROPOSAL.md). Embedded
browser/media player are skipped; IP-filter remains low priority. Swarm
merging has ordinary priority after the property tabs, or earlier if genuinely
required by a tab dependency. No torrent-property implementation or additional chapter started.
