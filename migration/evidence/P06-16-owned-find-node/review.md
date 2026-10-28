# Typed owned-loopback FIND_NODE/PING actor review

Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Full FIND_NODE
packets delegate to qualified P06-15 code. Original PRUDP/DHT packet handling
and DHTTransportUDPImpl.sendFindNode were inspected; immutable file/method hashes
are in original-policy-source.json. Original sendFindNode checks connection and
promotes contact instance/version/random ID/status/size/history before handlers.
This probe deliberately defers those host/routing state transitions, ordinary
retry/priority/privacy/statistics/scheduling behavior and actual token authority.
No full source integration baseline advances. Static reply random IDs are inert
wire snapshots, not storage authorization.

The existing OwnedPingTransport now has typed findNode callbacks and a20-byte
lookup target. Its original ping API remains. Both use one collision-checked
transaction map, pending/completion capacity, monotonic deadline owner, queued
coalesced callback delivery, cancellation/close/dead receiver behavior and drain
budget. Supported reply action, endpoint, connection and transaction are checked
before contact parsing; network/version remain normal header checks. A malformed
or foreign reply cannot retire the legitimate pending request. Monotonic deadlines
are checked before and after parsing. Scalar transaction reads precede decoded
owner moves. The packet buffer moves into the chosen decoder after prefix checks.

A supplied owned FindNodeReply snapshot carries status/size/random ID and<=128
contact records. Complete replies must fit1400 bytes; oversized snapshots fail
without truncation or sending. Incoming/returned endpoints remain inert data,
never destinations or automatic routing admission. The cache is constructed on
the actual Qt owner at start, including moveToThread before startup, and released
at close. All binds are new ephemeral numeric loopback, peer allowlists explicit,
per-socket NoProxy unchanged, and no public-mode/normal profile/core/plugin path
exists. Source Java uses unchanged ReferenceFence and connects only to its
explicit owned native IPv4 port. Native IPv6 is separate coverage.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan pass all16 CTest rows. The
existing PING Qt row has17 passes, the new FIND_NODE/mixed row13, zero skips.
Mixed IPv4/IPv6/network/version upgrade, typed replies/shared capacity, wrong
supported action, wrong endpoint/network, malformed/duplicate contacts, pending
retention, cancel/close/dead receiver, deadline/late replies, callback actor
deletion, reply budgets/API validation and moving the QObject to a real worker
thread before start are covered. All existing golden primitive/PING/node/full
FIND_NODE fixtures and bounds remain passing. Each compiler additionally passes
1251 source packet encoder comparisons/2502 Java accepted-failure readbacks and
849 node/contact Java readbacks/1024 fresh identities.

Actual UDP interoperability adds40 FIND_NODE exchanges over80 datagrams and
replays40 PING exchanges over80 datagrams per compiler, both directions across
five network/version cases. Exact source constructors/serializers validate
original targets and inert17-contact replies; IDs/network/negotiation match,
including cached51 upgrading to55. No normal Java service is started. There is
no Java IPv6 socket claim, desktop capture or access to running clients/profiles.

A first scaffold compile used a shared auto declaration for different expected
error types; separated before qualification. A development run inside the
network-restricted sandbox failed every loopback bind (including unchanged PING
and independent TCP bind tests). The same socket tests passed in authorized owned
loopback execution; logs for both are retained. No native runtime/golden mismatch
was found. The moved-thread test uses bounded owned-worker cleanup. Leak detection
is enabled without suppressions. No independent reviewer was used.

Exact123 compiled/tool/fixture inputs, nested original helper/core classes,
OpenSSL3.5.5/Qt6.12 runtime hashes, commands/logs and binaries are recorded. Six
narrow integration changes are identified by before/after hashes: actor sources,
CMake/dependency and README plus guards in two historical consumer CMake files to
avoid adding the now-shared node/FIND_NODE targets twice. No unrelated lower
qualified source was changed. Original Java HEAD/index remain untouched.
Immutable implementation hashes follow in a later provenance commit.

Next: original transport values and FIND_VALUE/STORE framing, actual anti-spoof
key/token/cache lifecycle, lookup scheduling, routing/storage/persistence and
plugin/host integration. No operational DHT, full client/plugin parity, release
or measured whole-client performance is claimed.
