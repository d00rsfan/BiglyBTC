# Original Azureus transport values

C++23 value-wire component pinned to original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, for subsequent FIND_VALUE/STORE and
the original DHT services needed by ratings/message-sync. No ordinary managers,
profile access, DNS, network, expiry/storage or plugin activation occurs here.

Single/array encoders borrow outgoing snapshots, including explicit null states.
Decoders own value bytes, originator contacts/IDs and raw trailing data. Packet
version and skew are supplied. Original signed-byte thresholds11/23/24 control
version/lifetime/replication, while timestamp addition wraps in64 bits. Flags
and lifetime are unsigned bytes; replication control and its shifted frequency
are signed. Value length is capped at512 bytes with the original length16 and
padded single-read behavior. The source pre11 serializer emits an int0 which its
pre11 reader does not consume; this asymmetry is preserved, including failures.
No new global protocol or storage state is inferred from these fields.

Value originators use the shared **unfiltered** single-contact codec, retaining
port0 and source hash/cache effects. Contact arrays independently retain their
original port0 filtering. The single-contact decoder owns its fields and reports
a consumed count; it does not copy the unused remainder for each originator.
Cache/provider/thread failures remain explicit and cannot invent IDs. A later
failed value retains earlier source cache effects. Incoming endpoints stay data.

Whole inputs/outputs are capped at8192 bytes. Arrays have unsigned16-bit counts,
but native capacity is bounded by actual received bytes and minimum element size.
Null/oversized fields and unsupported originators preserve source partial encoder
order for diagnostics. Failed partial output must never be sent.

The guarded reference runs exact original single/array serializer/deserializer
and anonymous getter bodies over qualified original contacts/frozen SHA1Simple.
Only the nested checked-exception namespace changes; an inert contact bridge
supplies the source interface/string getter without initializing normal managers.
Fixtures include3644 source observations and native tests add ownership, skew,
replication, unfiltered/mapped originators, limits/cache/crypto/thread cases.
FIND_VALUE/STORE packets, continuation handling, anti-spoof lifetime, lookup,
routing/storage/persistence and host/plugin activation remain pending.

`decodeValuesPrefix` supports nested packet sets: it owns decoded values/IDs and
reports consumption while omitting the unused trailing-byte copy. Existing
`decodeValues`/`decodeValue` behavior remains unchanged. The containing packet
owns the complete raw input and computes its final trailing view itself.
