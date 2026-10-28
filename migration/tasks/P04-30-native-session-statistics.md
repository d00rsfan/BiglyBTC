# P04-30 — Native application Activity and Transfers

Status: bounded session presentation qualified and published (2026-10-22).
User priority: fully Activity and Transfers first, then remaining Statistics views.
Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, ActivityView,
TransferStatsView, GlobalManagerStatsImpl and OverallStatsImpl. Native source
`292193b3e837d6bf36468d6c64ee8c982a693003`; qualification, portable publication and
remaining original-service differences are recorded in [P04-31](P04-31-connected-statistics-accounting.md).
Full Activity/Transfers and broad original parity remain in progress.

Connect the independent immutable native-session stream to current session
payload/protocol/combined counters and all six rates, actual session uptime and
Activity download/upload graphs. Keep operation telemetry distinct and retain
original saved totals/mark/history views. Do not sum incomplete operation tails
or saved torrent payload counters into application traffic. Do not call an
unqualified final/last observation a complete lifetime delta.

Retain bounded actual graph observations at Window/session scope across closing
and reopening Statistics. Clear them on a new profile or native engine epoch;
copies remain immutable. Unknown/failed intervals stay disconnected. Display
actual idle measurements without starting an engine merely to open Statistics.
Keep hidden/minimized rendering deferred, packet admission monotonic, scalar
navigation memory and fresh saved-history reads intact. Offline/unstarted and
failed current measurements have explicit source states; no fabricated first
rate, DHT traffic or performance claim.

Then implement compatible overall/lifetime/mark and minute-history writers in
separate reviewed stages. First freeze original Java save/update/mark behavior;
preserve unknown documents and use owned-profile atomic transaction/replay
fixtures. Saved counters, DHT components, current session, marks and minute
observations must remain separate domains. Original cache invalidation and
restart/Java switching need actual generated-profile roundtrips before credit.
No normal `.biglybt` profile or running Java process is an input to these tests.
