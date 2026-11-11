# Azureus torrent DHT on the existing shared UDP owner

Implementation `cfb6c6276bd473742bef9d8d2088987af3ff5886`, native tree `21001f1b1e8f97d7086dad6379722ce5aab56c0f`; original
BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. This is the checked active Public torrent
runtime chapter, not completion of all F09/whole migration responsibilities.
No real user profile, client or payload was opened or changed for these checks.

The standard engine now supplies revocable checked-admission targets to the same
Qt worker/SDK UDP attachment already used by Public rooms. Three independent
network roles use original ids0/3/4, original contact/version files and the saved
DHT network policy. No extra listening session or permanent JVM was introduced.
Normal torrent work runs without opening a chat tab or loading a chat identity.

Original tracker behaviour comes from `DHTTrackerPlugin`, `DHTControlImpl`,
`DHTPluginImpl` and `DHTPluginStorageManager`: SHA1(original20-byte hash), typed
seed/downloader values, crypto/TCP/UDP port encoding, Public/private/source and
backup selection, health-dependent limited-online tracking, and diversified
child keys. Native peer hints are rechecked against the current admission,
profile source and live permission before reaching the actual SDK handle.

Tracker URLs/passkeys are absent from the DHT DTO/encoder. Current source checks
also guard local GET responses. Stop/pause/role changes revoke shared tickets;
withdrawals include every receiver of an actual STORE attempt because missing
acknowledgements do not prove that the remote value was never stored. Metadata
proofs are memoised by stable file identities; no payload hash or completion is
inferred by DHT discovery. Public+I2P settings are preserved; this role uses Public.

Limits are explicit:8192 targets,8 active GETs,5 active publications,128 deferred
publications/child jobs,256 peer hints per bounded mailbox item,32 diversified
keys per traversal and retained offset set,4 levels and the existing packet/DB
quotas. Source preflights count against the publication cap. Completed operations
wait for a publication slot instead of silently dropping targets. Shutdown
revokes the socket witness before joining; cached original contacts finish their
bounded metadata save. Remote entries that cannot be withdrawn expire normally.

Statistics receives actual selected transport/router observations. The status
button reports bound networks, eligible targets, pending operations and measured
lookup/store/withdraw/peer counts; binding alone is not a claim of peer liveness.
Existing payload counters and previous Resume-preservation safeguards remain in
place. The previous overall single-provider facade is not given fabricated sums
of the three normal DHT transports.

## Validation

GCC16, C++23, Qt6.12, Release O2; JDK17 original transport oracle. Existing SDK,
Qt and build cache reused. No new compiler/dependency clone, namespace helper,
privileged network setup or real-client launch. Full output stayed in ignored
local logs. Six final current-image CTests passed:

- `azureus-torrent-source-and-codec`:300 assertions including exact codecs,
  original policy defaults/overrides, private/passkey exclusion and unchanged
  source memoisation.
- `azureus-torrent-actual-network`:generated cold/warm owned transfers with
  tracker/Mainline/PEX/LSD unavailable, Native IPv4/IPv6/encrypted IPv4, original
  Java UDP transport on networks0/3/4, actual diversified frequency/size peer
  discovery and payload transfer, all10/two publication keys, private forged
  ticket, fresh source revocation, pause withdrawal and joined profile release.
- `general-chat-room-service`:independent rooms, real message/history/unread
  lifecycle, diversified room discovery and bounded shutdown.
- `owned-ddb-control`, `azureus-database-runtime`, `general-chat-room-page`:
  tracker flags reach actual query intents; unsupported flags remain refused;
  local deletion preserves foreign origins/empty-key lifetime and capacity;
  existing UI/chat behaviour remains accepted.

Separate receive-only Internet probes from generated empty profiles observed
correlated replies independently on Azureus0 IPv4, BiglyBT4 IPv4 and Azureus3
IPv6. No torrent hash, registration or payload was announced by those probes.
They prove public network communication, not public Internet torrent transfer.
No sanitiser run or performance comparison is claimed this chapter.

Failed attempts remain local evidence:initial database cap64 rejected the new
owner; the test's redundant routing-start assertion and early sampled-counter
assertion were corrected; the Java oracle required map-only configuration and
normal storage mode; the real STORE acknowledgement/pause race was fixed;
explicit position lists repaired the sharded test router; publication-cap loss
was fixed with a bounded deferred queue. The legacy flag1 negative control now
uses unsupported flag4 and separately verifies original flags1/2.

## Remaining boundaries and next work

[The task card](../../tasks/P06-AZUREUS-TORRENT-RUNTIME.md) retains advanced original
remote-diversification persistence/expiry, queued scrape-only accounting, full
forwarded-value/local-diversification maintenance, separate UDP/proxy/filter
provider qualification and real repaired-library acceptance. Original `diverse`
files are preserved; stable current-registration PUT offsets are RAM-only.
Earlier unsupported clean Resume/storage layouts stay deferred rather than
rehashed or inferred from backup metadata. Real startup/steady CPU and the
separate tracker HTTP400 report still require later observation.

Next proposed chapter:Swarm Discoveries. It has not been started. Swarm merging
remains near the final browser/media-player work under the user's priorities.

Provider unchanged:upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b`,
overlay parent `4ff478031095830fcfb7531dd6bc0f21b30906af`,
qualified ELF SHA256 `8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3`.
Full artifact/source bindings are in RESULTS.json. Self-review; no independent
reviewer/subagent used. Public-commit privacy scan passed against the read-only
backup inventory; raw traces/temporary profiles are not committed.
