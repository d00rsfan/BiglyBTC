# Original local Azureus checkpoint foundation

Milestone source `a4c39d4e864f119b6e408a8ad7352466279b3004`, repository tree `1e235613458b9e0f638804f197d6753f0e41ef52`.
The chapter remains in progress; normal runtime activation is intentionally
unchanged until its asynchronous durable-response integration is qualified.

Original [BiglyBT](https://github.com/BiglySoftware/BiglyBT) baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`,
`DHTPluginStorageManager.storageKey`: serialise/deserialise, lazy expiry and
statistics. Existing selected policy thresholds/randomness are unchanged.

The policy exposes type/expiry checkpoint export and restoration without drawing
entropy or sampling time. Counters and IP bloom are volatile, as in the original.
The local original-file adapter reads the last live recognised record and merges
only requested keys. Known type1 updates clear all recognised duplicates for the
key. Unknown fields, unrelated records and the qualified remote list are retained;
ambiguous colliding records, pending and unsafe sources refuse publication. The
local and remote writers reuse the exact guarded owned-directory/CAS/staged-rename
publication implementation. An absent checkpoint clear is a no-op.

[Qualification](qualification.json):18 cases execute actual unchanged original key
methods, with an inert manager whose write callback only counts invocations. The
fixture tests lazy expiry and original14-byte statistics. Eight local-checkpoint
QTest totals, nine affected remote-publication totals and existing storage-policy
units pass; Qt totals include fixture lifecycle. Generated profiles cover
cross-list preservation, row/top unknowns, network separation, duplicate/expiry
selection, removal, malformed/pending refusal and no-op absence. No long simulated
window was replaced by minutes of wall waiting. Full logs stay outside Git.

Disposition: checkpoint API and file foundation implemented. Current normal DB
storageStats stays Absent. Owner seed restoration/change reporting, coalesced
source-broker persistence, post-operation response deferral and current-generation
validation remain next work. An RPC hint is no file/endpoint authority. There is
no activation, public-network, full DHT or performance-parity claim, no new daily,
and no live profile/process change. The same agent implemented and reviewed this
milestone; no independent reviewer was used.
