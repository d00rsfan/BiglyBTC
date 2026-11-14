# Original Public NAT wire formats

Native `9cf59d6124548cdb850a6efdd9d7568b56917235`, tree `0452ed4eb5824b5dd376fca437f3a705b069c385`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The codec reuses original-compatible UDP contact encoding, adds publication
version0, preserves the original raw node-ID + :DHTNATPuncher lookup input and
SHA-1 transfer handler, and encodes/decodes type0..11 messages. Numeric socket
binding keys use Java spelling, including expanded IPv6. Mapped IPv6 addresses
are canonicalised. Client data stays a bounded opaque map; no publication or
application instruction is performed. Original root _debug reflection is
explicitly refused. Unknown extensions otherwise remain ignorable; malformed
known fields, duplicate dictionaries, limits, bad contacts and trailing/version
publication errors are rejected. Active contact ports must be nonzero.

Five QTest entries passed in0.22s, including100 byte comparisons against exact
original builders across IPv4/IPv6 and protocol51/55. The oracle extracts the
three original contact/key/publication methods, seven request-map construction
prefixes and five contact/address/byte-array serializer bodies, using original
BEncoder. Memory transport/contact interfaces and CT_UDP1 are explicit seams.
Full original UDP interface/utility initialisers are excluded: instantiating a
full proxy started global Java configuration/security facilities during fixture
preparation. The final pure fixture is fenced against network/profile access.
No original asynchronous puncher/receive/timer or full-owner equality is claimed.
See [source scope](source-scope.json) and [qualification](qualification.json).

Whole NAT/private parity remains open. Next come bounded bind/expiry/failure and
client/punch states, then expected-peer observed-port ingress, three-party/shared
UDP qualification and actual publication/chat owner integration. Current DATA
admission requires the known actual endpoint and matching header originator;
normal direct endpoint tests cannot qualify a NAT port change. No broad ingress
exception is authorised by this codec. The running user image/profile/SDK and
launcher remain unchanged; no daily image was created.
