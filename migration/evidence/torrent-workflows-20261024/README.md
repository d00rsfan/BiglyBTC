# Compatible torrent workflows — implementation boundary

This commit follows core milestone8795a76eef634bc6167713d913af7d302e3e028d.
Original BiglyBT baseline: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
Replacement LT2.0.12 source:740a0b9aeabe00e762cc0efe4a0f27593db2550b.

Adds persisted original magnets, actual metadata-only storage routing and verified
retirement before ordinary import, pending UI/retry/destination controls and
original atomic force-start. New file imports select normal-priority files.
Essential per-admission raw counters survive pause/resume independently of the
optional peer observer and LT tick-lagged lifetime counters. Portable packaging
adds an explicit-profile launcher; the offline sample launcher stays available.

Initial connected GCC run passed16/19 suites. Subsequent failing runs are retained
as failures: stop_when_ready blocked metadata; ConfigFile refused the private
.torrent staging suffix; all_time counters lagged; import encoded files as skipped.
The raw-epoch run passed managed-download, magnet-intake, managed-seeding and
managed-library-command-routing, then was stopped after its known import failure.
Final affected import run passed3/3 (torrent-intake, managed-magnet, magnet-host).
No interrupted run is claimed as a complete successful batch. Source manifests
bind each actual run. Final secondary/hosted/private-runtime qualification and
exact final implementation hash will be recorded by a later provenance commit.

All profiles/endpoints/payloads are generated and owned. Running original Java,
ordinary profile/payloads and system installation were not accessed. Broad full
original engine/plugin parity, unqualified storage/proxy combinations and public
swarm performance remain separate migration work.
