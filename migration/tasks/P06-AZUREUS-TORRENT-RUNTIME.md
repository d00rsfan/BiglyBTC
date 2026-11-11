# P06 — Azureus torrent DHT on the shared UDP owner

User-authorised 2026-11-11. This chapter connects normal checked active torrents
to the original Azureus DHT; general Public chat discovery already preceded it.
Finish this chapter and report Swarm Discoveries next, without starting it.
The real Java client/profile/payload remain untouched throughout qualification.

Implemented scope:

- Independent bounded torrent discovery/publication in the existing SDK shared
  UDP worker. Azureus IPv4 network0, Azureus IPv6 network3 and BiglyBT IPv4
  network4 retain separate actors, routers, original contacts and value versions.
- Original SHA1(infohash) keys, downloading/seeding value flags, numeric TCP/UDP
  port and crypto requirement encoding; actual peers reach only their current
  checked engine admission. Mainline enable/announce is independent.
- Fresh original Public/private/peer-source/DHT backup/tracker health policy,
  original tracker prefix and limited-online selection. No tracker URL or
  credential enters DHT keys or values. Unchanged metadata is memoised.
- Source-bound publication and local serving; pause/stop/role changes revoke
  tickets. Tombstones target every actual STORE receiver, including uncertain
  acknowledgement outcomes. Close revokes the shared transport before join.
- Bounded frequency/size key diversification, stable size PUT offsets during
  the active registration; source-authorised cache export and cold/warm reuse.
- Actual shared transport/router observations in Statistics, with a lower status
  indicator showing bound networks, permitted targets and measured operations.

Acceptance and immutable implementation provenance are recorded in
[the review](../evidence/20261111-azureus-torrent-dht/REVIEW.md).

Explicit remaining whole-DHT parity work (not claimed by this chapter):

- Durable original remote-diversification ledger/expiry across application
  restarts; local storage diversification, forwarded-value republish completeness
  and queued scrape-only accounting. Current bounded active target forks work;
  original `diverse` files are preserved without conversion or replacement.
- Normal support for separate non-data UDP ports, proxy/IP-filter providers and
  advanced address overrides beyond the existing qualified shared policy.
- Unsupported clean resume/storage layouts remain deferred by the previous
  preservation milestone; no completion is inferred from an older backup.
- Real repaired-library startup, steady CPU, real tracker HTTP400 diagnosis and
  live Internet torrent transfer require later observation. Owned data transfer
  and public-node exchange prove different, explicitly separated behaviours.
- Shared normal DHT byte totals are not added to the existing single-provider
  overall accounting facade by this milestone. Payload accounting is unchanged;
  Statistics displays one actual selected DHT transport, not a fabricated sum.

No whole F09 or global migration completion is implied. Swarm merging, embedded
browser/media player and low-priority I2P/RSS/embedded tracker work stay deferred
under the user's current priorities. General Public rooms remain operational.
