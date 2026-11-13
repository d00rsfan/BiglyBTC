# Shared DDB packed and chained rating reads

Original BiglyBT baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f;
azrating baseline1629340340d0bc391d9e582f9ee989a93d550581. Native chain
implementation b37529256b0ab2ead465c565901bad59aed8e4bf, tree
1cf00b62fc510ef2e9296458f34c5a783e6a7b10. The no-allocation envelope
primitive is separately qualified in [its review](../20261113-ddb-value-envelope/REVIEW.md).

The shared Public rating owner splits exact-flag4 envelopes after per-query
originator-ID/raw-byte deduplication. Equal entries inside one packed value
remain separate DDB value events. Empty packed values produce no value event;
malformed bare values do, preserving the original owned-download fallback
condition. Generic hash reads never substitute local ratings or write download
metadata. Actual UDP callbacks retain source checks and a query epoch, so old
responses cannot apply to a subsequently reset query controller.

Header1 follows SHA1 of the current logical wire key, corresponding to Java's
SHA1(applicationKey) followed by the DHT key encoder. Queries are sequential and
same-key continuation requests coalesce. The native whole-operation deadline,
raw-byte4MiB and decoded-event16384 bounds are explicit finite limits; this is
not equality with unbounded original duplicate continuation/completion behavior.
The first chain checkpoint has16queries per lane; the following diversification
integration replaces this with a common32-query branch budget.

Five grouped CTests passed in47.55seconds after the final test executable linked:
shared-public-ratings, shared-ratings-original-java-transport,
shared-public-rating-hash-reader, shared-public-rating-hash-java and
shared-ddb-packed-chain-java. The new cases use the actual original Java UDP
transport on IPv4 and IPv6, generated packed duplicate values and a real next-key
response. A separate empty packed case distinguishes owned fallback from a
generic no-fallback read and checks unchanged download state. Java's test-only
request handler injects records; it is not the full original DDB scheduler.
An earlier test started before relinking was interrupted and receives no credit.
No sanitizer/platform/runtime-completion or full F12 integration claim is made.

Advanced read/write diversification is still active and will append its own
immutable source and verification scope. No live profile, user-facing image,
SDK, running process or preview manifest was changed.


## Qualified bounded diversification integration

Native6f5965654c38e41399989145a5936f2005ce0455, tree
14f36a13ba88387b8f56575b06dfc1262dd0267d adds real remote read and STORE
continuations. Original source policies are DDBaseImpl.listenerMapper.valueRead,
DHTControlImpl.getSupport/put and DHTPluginStorageManager.diversification.getKeys,
createNewDiversification and its expiry/fixed-offset constructor. The existing
source-qualified SHA1(key||offset) helper is reused. Only types2 and3 are accepted.

The owner consumes original-key values even when a reply requests diversification.
Frequency reads choose one of ten children; size reads choose two distinct
children. Frequency writes use all ten; size writes reuse two fixed offsets.
A per-family owner cache remembers the original type and fixed pair for48..72h,
then expires it. Subsequent operations follow known mappings before querying
the parent. This cache is bounded256entries/family and is deliberately memory
only; it does not claim original on-disk remote-diversification import/export.

Each query has its own raw originator/value deduplication and remaining raw-value
quota; equal values from separate keys remain distinct original DDB events.
Logical chain keys are separate from physical diversified keys: packed header1
continues SHA1(logical wire key), never SHA1(selected shard). All branches share
the operation deadline,32keys per family, depth4 and earlier byte/event bounds.
A limit failure cancels the result and cannot save a derived rating cache.
Same-key branches coalesce and execute sequentially. These are explicit finite
resource policies, not original task-demuxer, dynamic-estimated-depth, duplicate
completion or fully exhaustive DDB parity. Statistics/obfuscated queries and
arbitrary multi-value publication are outside this rating adapter; azrating
continues to publish the original single anonymous rating value.

Node lookup, store-token requests and STOREs all use actual actors and fresh
source permission. Query epochs retire queued replies before controller reset;
child STORE callbacks bind their real key and epoch. Publication counters count
actual replies, including the parent acknowledgement, without invented nodes or
confirmations. Local publication remains the original source-bound root value.

Final seven affected CTests pass in98.16seconds. New actual Java UDP tests cover
frequency/size reads and writes on both IP families, packed original-key values
and logical continuation, all ten or fixed two real stored keys, cached repeat
publication, held child replies after cancel/plugin revocation/owner close and
repeated-diversification depth rejection. Generic reads preserve generated
inventory and create no download state file. Prior bare owned/generic reads,
publication, cancellation and Java transport suites also pass. A first
standalone diversified group passed in30.88seconds. This is host GCC16/Qt6.12
qualification, not a new sanitizer run or full original DDB scheduler execution.

The bounded advanced-rating chapter is finished. Original remote-cache disk
compatibility, exhaustive/obfuscated/stats/general multi-value writes and full
completion policy stay explicit further DDB work. Next is optional original
per-download rating chat notices. No user-facing image or live data changed.
