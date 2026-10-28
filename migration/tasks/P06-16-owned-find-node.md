# P06-16 Typed owned-loopback FIND_NODE exchanges

Status: implemented owned-loopback slice; all three compiler/sanitizer matrices pass.
Evidence: migration/evidence/P06-16-owned-find-node/{matrix.json,review.md}.
Original baseline pinned to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f before implementation. Build on
P06-15 qualified full packets and P06-13 bounded Qt6.12 actor. Extend the same
pending/completion/deadline/cancellation/lifetime owner to typed FIND_NODE and
PING callbacks, sharing capacity and collision checks. Keep existing PING API.
Match the expected reply action as well as endpoint/network/transaction/connection;
reject foreign replies before node-cache parsing. Check monotonic deadlines
again after decoding. Never read header fields and move their owner in one call.

Only newly owned ephemeral numeric-loopback sockets and an explicit peer
allowlist are permitted. Responses use supplied inert contact/status/size/random
ID snapshots (<=128 contacts), with1400-byte final datagram budget and no implicit
truncation. Returned addresses never trigger sends, registration or recursive
lookups. Static random IDs are wire test values, not active anti-spoof authority.
Construct the borrowed node cache on the actual Qt owner at start, including
when the QObject was moved before start. Contact/provider/routing state remains
inactive. No public mode, DNS, discovery, proxy, profile or ordinary Java managers.

Add native mixed typed requests, wrong action/endpoint/malformed/duplicate replies,
shared capacity, cancellation/close/dead receiver, late replies, callback deletion,
response budget and owner-thread cases. Actual fenced Java/native FIND_NODE
exchanges must work in both directions using exact source-qualified packets.
Keep unchanged ReferenceFence (Java actual IPv4 only; native IPv6 separately).
Qualify fresh GCC16/Clang24/ASan+UBSan+LSan, all existing PING/node/packet tests
and actual PING Java datagram regressions. Record narrow integration input changes
and full immutable commits; preserve original Java HEAD/index/running profile.
Then continue source-qualified lookup scheduling and anti-spoof/routing/store
services required by dependent plugins. No operational DHT/release claim.
