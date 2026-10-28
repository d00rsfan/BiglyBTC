# P06-08 Original Azureus DHT UDP headers

Status: implemented bounded header codec, qualified locally; whole transport
and release verification remain pending. Ratings, related content and message-sync require the
original BiglyBT/Azureus DHT, distinct from Mainline DHT. Begin its native
transport foundation with request/reply headers and endpoint encoding at core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This task is a bounded wire codec;
PING extensions, actual UDP transaction/lifecycle dispatch, routing, contacts,
values/storage/persistence and full plugin services remain later tasks.

Allowed paths: native/services/azureus_dht, task-local source oracle/fixtures/
evidence, build consumers and shared tracking. Original Java sources/profile are
read-only. Use C++23 and the pinned GCC16/Clang24/libstdc++16 toolchains; no JVM
runtime dependency. No listener, DNS, discovery, router mapping, public network or
live profile is opened by the codec. Any eventual wire tests must use separately
owned numeric loopback endpoints and a source fence.

Pin PRUDPPacket{,Request,Reply}, DHTUDPPacket{Request,Reply}, DHTUDPUtils,
DHTTransportUDP version constants/Helper and DHT network IDs before coding. Source
request/reply serializer/deserializer bodies may use supplied immutable transport
getters, network registration, clock and minimum-version context in the helper;
record this explicitly rather than claiming normal DHT transport construction.
Prefer unchanged original PRUDP/address helpers where safe. No manager/plugin/
CoreFactory startup or global configuration mutation is needed.

Characterize IPv4/IPv6 and mapped-address normalization, unresolved output,
unsigned ports, signed-byte protocol/version/flags, network-specific minimums,
the global DHT.protocol.version.min int32/byte narrowing, protocol negotiation,
instance/transaction/connection IDs, timestamp/skew wrapping and source processing
time short conversion. Serialization intentionally emits original local vendor
and originator version; do not assume decode/re-encode preserves a foreign
header byte-for-byte. Preserve opaque body bytes separately and return consumed
header length. Unknown network dispatch belongs to an explicit host context;
never map another network silently into the public one.

Run differential generated fixtures, failure/truncation/native-limit checks,
GCC/Clang/ASan+UBSan+LSan and source-built Java readback of native emitted headers.
Do not call a codec an operational DHT, answer real lookups or advertise missing
capabilities. Record exact immutable source/native hashes and all scope limits.

Result:7379 original observations, two CTest rows and3292 source-built Java
readbacks per compiler pass on fresh GCC16, Clang24 and ASan+UBSan+LSan builds.
See migration/evidence/P06-08-azureus-dht-headers/{matrix.json,review.md}.
Continue with PING extensions and owned-loopback transport before integrating
ratings/message-sync services. No listener/runtime/profile activation occurred.
