# Owned FIND_VALUE continuation review

Original source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The existing
owned-loopback actor now delivers typed FIND_VALUE replies without consuming the
callback on the first continuation. Pending and queued parts share callback
ownership; only final/error retires pending. Deadline remains fixed, checked
before and after decoding. No source packet history chain or sequence number is
invented. Source PRUDP setReply/setException and UDP sendFindValue file/method
hashes are recorded in original-policy-source.json. Full routing/contact promotion,
priority/privacy/retry/DB/nonce/ordinary profile policy remains deferred.

Endpoint/action/transaction/connection bind before contact body/cache parsing;
scalar transaction and continuation are copied before moving decoded storage.
Pending plus queued events retain the existing hard cap. Lack of room for a
partial event terminates with Capacity while already queued parts stay ordered.
This is explicit bounded native probe policy, not full source scheduler parity.
Callbacks retain cancel/close/reentry/throw/deleting-actor safety. Dead or moved
receivers are abandoned; no callback crosses Qt owner threads. Cache construction
still occurs at start on the actual actor owner after any thread move.

Inert response pieces own value bytes/contacts. Startup bounds16 pieces/256
records/512-byte payloads/128KiB total payload. Each encoded datagram must fit1400
without truncation. Value replies apply incoming request skew. No STORE database,
anti-spoof authority, routing promotion, DNS or ordinary application/plugin starts.
The three historical CMake consumers now guard shared targets; all other prior
qualified input bytes remain unchanged.

Fresh GCC16/Clang24/ASan+UBSan+LSan each pass23 combined CTest rows with47 Qt
passes (17 PING,13 FIND_NODE,17 FIND_VALUE), zero skips. Tests cover three-part
streams and final, native IPv4/IPv6, mixed typed operations, partial then timeout/
cancel/close, late final, queue exhaustion with retained early parts, dead/moved
receivers, callback exceptions/actor deletion, owned snapshots and foreign/type/
network/malformed packet binding. Each compiler also passes actual fenced
source-Java/native IPv4 datagrams in both directions:40 FIND_VALUE streams,
120 reply parts,160 datagrams, across five network/version/upgrade combinations.
Prior PING/FIND_NODE adds160 datagrams, total320/compiler. Java IPv4 coverage is
separate from native IPv6; ReferenceFence remains unchanged.

Every compiler repeats all qualified FIND_VALUE/STORE/value/contact/FIND_NODE
serializer and source-readback suites. Lower packet fixtures are unchanged.
Compiled/tool/fixture input hashes, source/helper/core/runtime/binary hashes and
all commands/logs are in matrix.json. Leak detection is on with no suppressions.
No wire mismatch or compiler/test failure occurred in this bounded slice.
No independent reviewer was used. Full source integration baselines stay null;
no operational DHT, live-profile/plugin activation, release or whole-client
performance improvement is claimed. Original Java HEAD/index/running client and
live profile remain untouched. Continue real anti-spoof authority and authorized
STORE/lookup/routing/storage/persistent host/plugin services without stopping.
