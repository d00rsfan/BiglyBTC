# Original Azureus DHT transport foundation

Bounded C++23 request/reply header and endpoint codec for original
BiglyBT core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This is distinct from
Mainline DHT and does not enable a listener, routing, value store or plugin.
No user profile, DNS, discovery or public network is accessed by the codec.
P06-08 records characterization and required integration before runtime use.

`encodeHeader` consumes supplied immutable host fields. `decodeHeader` owns the
received packet and returns the consumed header offset, opaque body and parsed
fields. Copies own their bytes; moves clear the old views. No configuration,
clock, transaction generator or network registration is inferred from the host.
An explicit registered-network context is required; unknown networks never fall
back to the public network. Optional skew observations are returned as data,
including failures after the original skew callback would have occurred.

Signed Java byte comparisons, int32 minimum-version override narrowing, wrapped
int64 clock subtraction and short processing-time conversion are preserved.
DataInputStream partial-field consumption is preserved on truncation. Mapped
IPv6 normalizes to IPv4 on decode while the original body offset stays intact.
Unresolved outgoing addresses serialize as0.0.0.0:0 without DNS.

The original header serializer rewrites vendor to the local value, originator
version to the current local version and decoded reply processing time to zero.
`reencodeHeader` exposes that header-only projection; `rawPacket` and `body`
retain incoming bytes without rewriting them. Future dispatch must select the
request/reply kind; this library does not interpret action payloads or PRUDP
connection-bit multiplexing.

The native packet cap is8192 bytes, including opaque body (original PRUDP maximum).
Malformed lengths and invalid endpoint widths are rejected with consumed offsets.
Normal DHT1400-byte datagram budgeting belongs to the future transport. Headers
alone do not prove peer identity, an active transaction or advertised capability.

Standalone builds use CMake, C++23 and the pinned native toolchain. CTest compares
7379 source-generated observations and checks limits, ownership, partial failure
diagnostics and mapped-address/body offsets. Migration qualification also runs
3292 source-built Java readbacks per compiler, with leak detection enabled for
the sanitizer build. The reference copies exact original header/endpoint/length/
minimum method bodies into a guarded helper with supplied transport/contact
getters, clock, network registration and IDs. It does not construct a normal
Java DHT transport or claim complete transport behavior.

PING extensions, UDP transaction/lifecycle dispatch, routing, contacts, values,
storage/persistence, plugin integration and live-profile compatibility remain
required. This module enables no network traffic and is not an operational DHT.
