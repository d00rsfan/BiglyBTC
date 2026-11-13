# Generic Public rating reads and original RCM lookup cells

Native `7661655651e7d0b04660e768d0ad63752e2a14a4`, tree
`a209a6b6495ab09bc96534b8f172c485fa9eb09a`. Original azrating
`1629340340d0bc391d9e582f9ee989a93d550581`, aercm
`cc18cdc42c36a33b435d78e3d86db64399058ac1`, BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Builds on the independently
qualified shared owned-download rating owner and original rating data/key model.

The original generic lookupRatingByHash is independent of download membership.
The new typed PublicRatingHashSnapshot carries only the hash, original key and
read-source revision; it cannot pretend to contain an owned local contribution.
The shared actor uses an explicit owned/hash source variant,30-second generic
read budget, retained original rating records, active profile generation and
fresh original global plugin/DHT/Public bind checks. By-hash work receives local
scheduling preference among rating jobs. No owned fallback, derived globalRating
write, torrent/queue/resume update or publication is performed by this reader.
No fake download is created for an unknown hash. Separate result namespaces
prevent an empty generic result from replacing an owned rating/fallback. Repeated
active requests coalesce, and cancelled/late responses cannot restore a result.
Only the Public provider is implemented here; this is not a claim about every
original network-selection or cross-service priority-scheduler path.

Swarm Discoveries uses its original rc_rating column and an explicit Lookup
click, including transient keyword-search results. Showing rows alone performs
no request. Completed zero-rating reads show None. Original retained scores are
clamped1..5 for this particular column, summed/divided in binary32, widened to
double before floor, and formatted without a trailing .0. Comments use original
ASCII trim and fallback text; arbitrary UTF16 units are retained. This preserves
the original widened-float quirk: scores5,4,4,4,4 display4.1 in RCM, while the
owned-download column's different original arithmetic displays4.2. It is not a
new cross-view normalization. Tooltip comments are escaped for Qt rich text.

Immutable result maps are reused until actual changes. Rating-only updates edit
cells in place without resetting rows, selection or horizontal/vertical scroll.
The cache remains bounded by the existing128-result owner limit. The generic
facade is reusable by later original consumers such as Open Torrent Options;
this chapter does not implement that low-priority addition flow, non-Public
60-second hash reads, full RCM loading animation or all external plugin IPC.
The original owned-download20/80-second read contracts remain separate.

Validation: six affected CTests pass in35.68seconds: editor20Qt rows, existing
RCM page4, shared owned ratings10, original Java owned rating transport4, new
native generic reader5, and RCM projection/UI4. Generic tests prove IPv4/IPv6,
unknown-hash reads, stale-generation rejection, owned fallback/cache isolation
and unchanged/absent torrent state. A final actual delayed-reply coalescing/
cancel test passes3Qt rows in1.686seconds. Additional arbitrary-hash exchanges
with the pinned original Java UDP transport pass4Qt rows in4.82seconds across
IPv4 and IPv6, with no missing-metadata or state-write workaround. Other passing
suites were not repeated for that final coalescing branch.

The original aercm refresh method is extracted verbatim, compiled with explicit
in-memory cell/result and English translation adapters, and compared on164
completed result/error cases. The comparison executed with zero skips. It covers
clamping, widened binary32 arithmetic, ASCII-vs-NBSP trim, BOM/unpaired UTF16 and
a null-comment error outcome. [Source scope](source-scope.json) records file and
method digests. This is finite original-method qualification, not actual SWT or
plugin IPC runtime equality. The UI test proves explicit intent, no duplicate
lookup after caching, escaped tooltips and stable model/viewport/selection.

The original network test group now explicitly selects its original scenarios;
owned rating, generic reader and Java rating groups are separate, avoiding
repeated scenarios and artificial growth of the aggregate timeout. Static host
and affected tests compiled with GCC16/C++23/Qt6.12. No new sanitizer/platform or
full migration claim is made. No daily-use executable, SDK, running application,
real profile or runtime manifest changed. Advanced DDB diversification/packed
values and optional original rating-chat notices remain in the plan. Continue
those source chapters without stopping; prepare a user build only when asked.
