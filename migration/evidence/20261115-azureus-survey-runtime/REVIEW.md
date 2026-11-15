# Source-gated Azureus replication survey runtime

Native implementation `4da880af94aa6fca5539a02ca6e6ca76b491b8cd`, native repository tree `8e0ce7b0afb004724d04c4c49552295d20eddd87`.
Original source baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
This integrates the previously qualified original eligibility, topology, repair,
contact-state, bounded DB-candidate and saved-policy prerequisites.

Actual seeded router state and source-owned DB candidates drive bounded
neighbourhood lookups, QUERY_STORE and token-first repair STORE. Independent
DB snapshot cycles freeze mapping incarnations and store receipts. Pages use
64 keys/256 raw values; no whole-namespace payload snapshot is retained.
Exact encoded requests stay within 1,400 bytes, with at most three active
query/repair peers. Current profile/source gates precede each network step;
repair checks the same mapping incarnation and content/store receipt again.
Late callbacks, revoked sources, timeout, cancellation and close cannot resume
the cancelled operation. Actor callbacks do not perform filesystem I/O.

Knowledge tracks original failures, instance changes and expiry. Original
150-contact LRU and explicit 8,192-mapping knowledge bound keep retained state
finite; pressure clears only knowledge and causes another query, retaining
failure/instance/time data. IPv4 subnet assumptions and strict closest-K rules
use the previously characterised original planners. Failed replies and wrong
sizes do not create fabricated successful copies. Request tokens are acquired
through the actual existing wire path, not inferred from QUERY_STORE replies.

The normal profile worker captures original saved feature flags once at actor
creation. Ordinary DHT remains active when bit0x20 disables replication surveys.
Normal cadence is fifteen minutes; accelerated tests require the existing
owned-loopback factory and are not profile/network settings. Each normal DHT
instance now receives an actual random startup instance, matching original UDP
contact behaviour and allowing neighbours to invalidate knowledge after restart.

Owned IPv4/IPv6 tests exercise lookup/query/token/repair and a warm second pass
with no duplicate STORE, source revocation, held permits, timeout, cancellation,
close, unseeded refusal and an actual same-endpoint remote restart. Generated
normal-profile tests exercise enabled repair, saved disabled policy and actual
local-sender instance propagation. Actual original obfuscated lookup targets
are compared on five fixtures. Query/cache regressions and affected normal chat,
torrent privacy, ratings, durable checkpoint and forced-seed Pause/Stop bridge
checks pass. One coherent application link passes with GCC16 Release/C++23 and
Qt6.12.0; detailed group counts are in qualification.json. Earlier failed
development attempts remain unqualified; unchanged original planner proofs are
carried from their separate recorded commits, not claimed as rerun here.

No live-profile acceptance, public-network convergence, fresh daily image or
whole F09 completion is claimed. Signed key-block verification/serving and
maintenance follow; other remaining F09 coverage stays open.
