# Azureus cached-value republishing and maintenance

Continue autonomously after normal local checkpoint integration
`c75a7113457bd97d5cd5170562ce7ace06b874c0`; no intermediate daily image.
Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` is authoritative.

Port `DHTDBImpl.republishCachedMappings` with original per-value age, clock
rollback, local-value exclusion, diversification skip, default versus explicit
replication handling, closest-node cache ownership and one final forward before
cache removal. Relay values must preserve origin/version/flags/lifetime while
using the actual current local sender. Keep the existing original token-first
forwarding rule; do not weaken unseeded/closest/authority admission for tests.

First qualify actual original selection and relay projections, then extend the
bounded DB owner with immutable owning candidates and stale-content receipts.
Native planning alone grants no network/profile permission. Integrate coalesced,
bounded normal-owner jobs with current per-network source gates, routing contacts,
anti-spoof requests, MTU splitting, cancellation, retry/clock bounds and no actor
filesystem I/O. Never duplicate ordinary DB reads to validate maintenance.

Explicit-replication survey and signed key-block forwarding are dependent parts,
not implicitly equivalent to ordinary cache republishing. Record any remaining
parts honestly rather than silently forwarding survey-only values by the default
path. Preserve ordinary local publications and private/disabled torrent fences.

Use generated profiles, controlled operation clocks and actual original Java
fixtures to avoid long wall-clock green tests. Run focused module checks first,
then one application build at coherent integration completion. Keep compiler logs
on disk and inspect summaries/root errors. Record full source commit/tree and
upstream coverage before declaring completion; main only. No new daily unless
requested, no agents, backups, old image collection or live-profile writes.

Selection prerequisite qualified in `95e04a1213d9ae809e91e63c0c6f46843f792c29`; [review](../evidence/20261115-azureus-cache-selection/REVIEW.md). DB/normal-owner activation remains in progress.

Owning DB candidates and stale-content receipts qualified in `80ba30777345c37b442ca3f2ed2d8d9cb10cb88a`; [review](../evidence/20261115-azureus-cached-mappings/REVIEW.md). Normal forwarding remains in progress.

Bounded normal runtime qualified in `4fc79ab5f20079186247c534b173d62a018b7a16`; [review](../evidence/20261115-azureus-cache-runtime/REVIEW.md). Continue large-cache paging/packing and dependent survey/key-block work. This chapter remains in progress; no new daily image.

Paged normal runtime qualified in `93a6a3c29df3dbe8123348ca39b8b4835dbf2c9a`; [review](../evidence/20261115-azureus-cache-pages/REVIEW.md). Continue dependent survey/query-storage and signed key-block work. This chapter remains in progress; no new daily image.
