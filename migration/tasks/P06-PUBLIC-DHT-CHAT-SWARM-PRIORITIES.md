# Public Azureus DHT, chat rooms and swarm workflows

User priority decision: 2026-11-08. Distributed-feature implementation has not started.
Current work is critical existing-library resume/column/exit fixes. Stop after those
fixes and qualification; await an explicit request for the distributed chapter.

Delivery order: (1) working Public Azureus DHT, (2) chat rooms,
(3) swarm discoveries. Swarm merging is deferred until immediately before the
embedded browser and media player; browser/player are the final features in
the migration plan. Mainline DHT remains available.
I2P, RSS and hosted-tracker extensions remain lower priority.

## Dependencies and completion criteria

A. Connect the existing Azureus UDP/routing/database primitives to the normal
application lifecycle. Fill gaps in bootstrap, maintenance, contact persistence,
peer lookup/announce and shutdown, using the original configuration and network
identifiers. Respect IPv4/IPv6 support and bind/port policy of each original
network; do not assume every network is dual-stack. Verify Java/native exchange
and real application peer delivery, including restart and offline recovery.
Keep private torrents and tracker credentials out of DHT announcements.
Existing owned-loopback primitives are a starting point, not public activation.

B. On A, complete the distributed-database/transfer contract needed by consumers:
registration, lookup, request/reply, cancellation, timeouts and bounded resources.
Preserve original identity, value formats and Java/native profile round trips.
This is shared infrastructure, delivered within the Azureus/chat integration,
not a new unrelated chapter or a requirement to finish every DHT feature first.

C. On A+B, finish azmsgsync/buddy chat integration and Qt chat rooms: join,
receive/send, message ordering/deduplication, reconnect, persisted history and
identity, and relevant existing room settings. Include private-session security
where required by the original Public chat workflow. Test against Java and
preserve original chat files/keys. Public rooms first; I2P-dependent rooms stay
explicitly deferred. Existing envelope/STS primitives do not establish working
chat rooms.

D. On A+B and the existing library/UI, implement swarm discoveries through the
original related-content semantics and saved settings. Reuse discovery results
without exposing private torrents, disallowed networks or tracker secrets.
Chat is earlier in delivery order, not a protocol dependency of discovery.

E. On the existing file/storage/verification engine, implement swarm merging:
identify eligible matching files across torrents, safely reuse verified data,
respect offsets, selections and priorities, and verify destination pieces with
the destination torrent's hashes before advertising completion. Cover unequal
piece boundaries, partial/corrupt files, cancellation and restart. Matching
names or sizes alone do not establish valid data. Discovery can supply candidates;
merging itself must also work for eligible torrents already in the library.

Graph: A -> B -> C; A+B -> D; storage/verification -> E; D may supply E candidates.
Deliver C before D. Defer E until the late merge/browser/player work; it must
not delay DHT, chat rooms, discovery or ordinary-client usability.

## Development cost and identity

The user is the sole code collaborator. Do not repeatedly compare unchanged
working trees, local commits, remotes or source hash inventories. Keep the
required source/version provenance once per delivered milestone. Batch changes,
build once per meaningful milestone, run affected behaviour/security regressions,
and retain full logs on disk, reading summaries or relevant failure excerpts.
Public-commit privacy review remains necessary.

The current ordinary client inherits the provider identity:
HTTP User-Agent and BEP 10 v = libtorrent/2.0.12.0;
20-byte peer_id prefix = -LT20C0-, followed by 12 generated characters.
UDP announces have peer_id but no HTTP User-Agent.
A later identity change should name BiglyBTC truthfully and retain the engine
version, with an announce/handshake regression; do not impersonate Java BiglyBT.
No client-identity or runtime change is made by this plan.
