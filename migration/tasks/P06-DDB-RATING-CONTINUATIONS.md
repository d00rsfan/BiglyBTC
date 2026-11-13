# Advanced DDB value adapters for shared ratings

Next autonomous chapter after generic Public/RCM rating reads. No daily-use
image, new agents, live profile/SDK changes or private data in Git. Group checks;
commit/push main with full original/native provenance. Original BiglyBT baseline
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f and azrating baseline
1629340340d0bc391d9e582f9ee989a93d550581 remain pinned.

Inspect actual DHTControlImpl get/put and storage-adapter diversification and
DDBaseImpl packed/chained multi-value behavior before editing. The current
shared rating actor qualifies ordinary bare single-value values only. Original
related-content and torrent owners already contain bounded diversification
primitives; reuse qualified key derivation and permissions where applicable,
without assuming identical completion/budget semantics.

Implement actual diversified read/write continuation with bounded depth/fanout,
whole-operation deadlines, per-network originator/value deduplication, fresh
source permission per effect and actual actor cancellation. Avoid fabricated
local RPC nodes or publication acknowledgements. Retain the original single-
value path and source-bound local serving. Qualify source policies with original
fixtures and actual owned/Java peers, including limits/late/close and no metadata
writes for generic reads. Distinguish finite coverage from full DDB parity.
Original optional per-download chat notices follow this chapter; non-Public
providers and subscription/lightweight publication remain low priority.

First finite primitive milestone:
ffffc58a625b0657565298d3c065101a981f7551,
tree2eb17fbb7ea37935f0019cde110e93fdb2bb943a.
[Review](../evidence/20261113-ddb-value-envelope/REVIEW.md) records809 selected-
body observations, exact flags, partial errors and explicit no-runtime scope.
The chapter remains active; integrate actual chain/diversification scheduling next.
