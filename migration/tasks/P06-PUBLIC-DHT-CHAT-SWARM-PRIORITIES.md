# Public Azureus DHT, chat rooms and swarm workflows

User priority decision updated 2026-11-09: general Public chat rooms with many
participants now come first. [Acceptance and minimum dependency path](P06-GENERAL-PUBLIC-CHAT-ROOMS.md).
This is a planning update; implementation has not started.

Delivery order: minimum Azureus facilities needed by chat -> general-room
azmsgsync integration and UI -> remaining Azureus torrent discovery/announce
parity -> swarm discoveries. Full Azureus/DDB completion must not postpone chat
behind unrelated facilities. Mainline DHT remains available.
I2P, RSS and hosted-tracker extensions remain lower priority. Swarm merging stays
immediately before the embedded browser/media player, which are last.

## Dependencies and completion criteria

A. Connect the chat-required subset of the existing Azureus UDP/routing/database
primitives to the normal application lifecycle first; finish the remaining
torrent-facing Azureus integration after general Public rooms work. Fill gaps in bootstrap, maintenance, contact persistence,
peer lookup/announce and shutdown, using the original configuration and network
identifiers. Respect IPv4/IPv6 support and bind/port policy of each original
network; do not assume every network is dual-stack. Verify Java/native exchange
and real application peer delivery, including restart and offline recovery.
Keep private torrents and tracker credentials out of DHT announcements.
Existing owned-loopback primitives are a starting point, not public activation.

B. On the chat-required subset of A, complete the DHTPluginInterface/registered
transfer-handler contract consumed by azmsgsync:
registration, lookup, request/reply, cancellation, timeouts and bounded resources.
Preserve original identity, value formats and Java/native profile round trips.
This is shared infrastructure, delivered within the Azureus/chat integration,
not a new unrelated chapter or a requirement to finish every DHT feature first.

C. On A-minimum+B, finish azmsgsync/buddy general Public chat integration and Qt
chat rooms independent of torrent selection: join,
receive/send, message ordering/deduplication, reconnect, persisted history and
identity, and relevant existing room settings. Preserve general-room sender signatures and encryption. Private-session STS
integration is not a prerequisite for this first group-room delivery. Test against Java and
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

Graph: A-minimum -> B -> C; C precedes A-remainder in delivery order;
A+B -> D; storage/verification -> E; D may supply E candidates.
Deliver general rooms C before remaining torrent-facing Azureus work and D. Defer E until the late merge/browser/player work; it must
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
