# Original Public NAT wire prerequisites

This library contains codecs, without sockets, profile access or a NAT owner.
The original handler is SHA-1 of Aelitis:NATPuncher:TransferHandlerKey. Requests
and replies use the original type0..11 bencoded maps. Contacts reuse the existing
original UDP contact codec; publication prepends version0. Publication lookup
input is node ID followed by :DHTNATPuncher, before the normal DHT key hashing.

Socket-address binding keys use Java numeric InetSocketAddress spelling,
including uncompressed IPv6 in brackets. IPv4-mapped IPv6 publication/address
input is canonicalised as Java InetAddress.getByAddress does. Client data stays
a bounded opaque dictionary. The codec never publishes it, treats it as an
application instruction or executes original _debug reflection; root _debug is
refused. Duplicate dictionary keys, malformed types, invalid contact envelopes
and oversized/deep messages fail locally. Active endpoints require a nonzero
port; unsupported publication versions/trailing bytes are refused.

The pure original-method oracle extracts contact/key/publication methods,
request-map construction blocks and five exact serializer methods. Its memory
interfaces deliberately omit full Java UDP utility/interface initialisers,
which would start global configuration/security facilities. Original BEncoder
still produces the compared map bytes. This qualifies formats and generated
inputs; it does not qualify original asynchronous NAT/router/owner behaviour.

Next: bounded binding/client/punch state and receipt/source policy, explicit
NAT-only ingress for an expected peer's observed port, owned three-party/shared
UDP exchanges, then actual owner/publication/chat integration. Direct endpoint
tests do not establish NAT traversal or full private-conversation parity.
