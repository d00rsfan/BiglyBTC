# Survey topology and repair decisions

Native `11bf8e16a4bd74fd1246a6a4b55e371c08084343`, tree `705ae45d32b8ac4a442217dc12bcb97a63716cd7`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The actual original processSurvey/doQuery/queriesComplete methods execute in an
isolated generated-profile JVM with explicitly controlled contact callbacks.
Fifteen cases compare complete query groups and per-contact repair key sets:
missing/enough/one known copy, wrong tails, failed/short replies, K-capped factor,
local node absent/outside K, IPv4 shared subnet, token-first repair, common-prefix
compression, IPv6 and signed protocol versions 25 and -1. Original relays use the
actual local sender. The original callback driver also captures initial/max
pending counts and token calls; it does not open sockets or grant native peers.

The native owning graph copies its node/key observations, chooses sorted strict K
and schedules a key only when local is among them. Prefix extension follows the
original three-depth cost test; every suffix remains associated with its original
key row. Requests below protocol 26 remain conceptual failed-query work, with no
wire eligibility. Protocol bytes remain signed as in Java. Fixture comparisons
normalise group/row ordering because Java's map iteration order is not protocol
authority. The graph is bounded to 100 nodes, 8192 unique keys and K at most 64.

Repair decisions use supplied current contact knowledge, distinguish whether a
reply contributes to copy counts, cap the original maximum replication factor
to K, prefer contacts with fewer than two failures and then XOR proximity. IPv4
peers sharing /16 consume their selected candidate slot as an assumed copy; no
farther replacement is selected. IPv6 is excluded from that original subnet
heuristic. Changed router identity or mismatched observation identities refuse
planning. A result grants no STORE nonce, source permission or network effect.

GCC16/C++23 focused build and the actual-reference comparison passed: 15 cases
and five input-bound groups. A missing explicit span include was caught and
corrected with the three-error diagnostic cap. No full application build was
repeated for these currently inactive preparation helpers; no new daily or live
profile acceptance was performed. Contact-state lifecycle/record incarnations,
DB candidates, neighbourhood lookup, query aggregation and real repair forwarding
remain unfinished dependencies. F09 remains in progress.
