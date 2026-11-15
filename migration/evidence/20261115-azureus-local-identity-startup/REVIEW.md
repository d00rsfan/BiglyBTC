# Original-compatible Azureus address cache and startup identity

Native source `1246f452de04534badfe87607bc5eae2316a9955`, repository tree `c867ed6bc27b04bfe15eb9e6ce346c3395b34c1a`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

This completes the current startup/address-cache chapter, not hot transport
rebinding or whole F09. Actual original DHTPluginStorageManager methods supply
24 fixed-clock read/write cases and six InetAddress host-text results. Native
matches healthy numeric-address cache bytes, exact membership, most_recent,
strict seven-day pruning only on writes, reserved-key handling and Java signed
long overflow. IPv6 text uses original expanded groups; mapped IPv4 formatting
also matches actual Java. Parsing/comparison of the actual endpoint prevents
compressed/expanded IPv6 text alone from invalidating direct signed blocks.

Separate typed address-area and block-area receipts share the fixed-directory
capture/atomic publication implementation. Its private role check prevents
cross-file publication; network0/network3, independent preimages, pending-file
refusal and existing block/diversification regressions pass. No arbitrary path
is derived from the cached address. Unsupported existing cache data is retained.

Before handing a new State to its UDP actor, the profile worker clones verified
signed records and compares the cached healthy address with the actual observed
or owned bound endpoint. A changed host purges direct entries and durably saves
that purge before recording the new address. Historical cached-address
registration is coalesced with new-address recording, preserving original
startup history without claiming that the cached address is a socket binding.
An address publication failure retains existing files and the earlier durable
purge; a stale block preimage cannot publish a new address. Missing/unreadable
optional history does not erase trusted blocks. Cancellation writes nothing.
This is deterministic publication-order evidence, not a process-kill crash test.

Actual bound IPv4/IPv6 sockets exercise same-host retention, Java-expanded IPv6,
changed-host purge, interrupted address publication and stale preimages. Actual
generated normal-profile owners additionally prove same/changed/unreadable
startup behavior; key-block restart, scheduled forwarding, query and survey
regressions pass. An activation-in-flight guard prevents duplicate startup work.
The actor does not perform profile I/O, and an already bound actor's State is
not mutated from the profile worker. Signed positive fixtures remain explicit
owned generated-key tests; normal public construction uses the official root.

One coherent GCC16 Release/C++23/Qt6.12.0 application link plus affected chat,
torrent privacy, ratings, checkpoints, forced-seed Pause/Resume/Stop and DHT wire
checks pass. Earlier fixture import/dry-run preparation failures are unqualified;
the accepted groups/counts are in qualification.json. No live profile was opened
or modified during development. The requested daily is recorded separately.

Actual hot address/port changes, endpoint-report policy and complete router/
contact/query/survey/session rebinding remain an explicit follow-up in the plan.
No full local-identity/F09 baseline is advanced. At the user's explicit request,
prepare the daily after this chapter and pause without starting that follow-up.
