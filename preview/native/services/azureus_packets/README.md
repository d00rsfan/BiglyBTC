# Complete original Azureus PING packets

C++23 request/reply packet assembly at original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This composes the qualified header,
request-option/upload, position and alternative-contact wire components. It
starts no network, Java/core/plugin manager, DNS, statistics collection or profile
access. It has no Qt or JVM runtime dependency.

`encodeRequest` and `encodeReply` borrow host snapshots for the call and return
owned bytes. Version/network gates derive from the encoded header. The reply
selects the first16 contacts before array encoding, as the original list setter
does. Partial serializer failures retain preceding header/body bytes only as
diagnostics; failed outputs must never be transmitted. Supplied property maps
must already be encoded. Cached upload data, provider registration/lifecycle and
statistics policy remain host responsibilities.

`decode` owns the original packet, header and decoded body. Header and total
consumed offsets stay separate, and trailing bytes derive from the original
packet. Mapped IPv6 normalization therefore cannot move the payload. Copies own
their data, copy assignment commits a complete owner, and moves clear the old
offsets/views. Header skew diagnostics survive later required-body failures.
Known provider reset/signed skip and padded contact properties affect following
fields exactly as in the component codecs. Optional upload failures return no
statistics at their actual consumed offset.

PING actions1024/1025 dispatch after the complete PRUDP request16/reply8 prefix;
other actions are rejected before the corresponding header constructor. The
codec accepts an explicit request/reply kind. Connection-bit multiplexing,
endpoint/transaction/connection validation, deadlines, cancellation, pending
queues and ordinary1400-byte DHT datagram budgeting belong to the next transport.
The codec retains the8192-byte original PRUDP cap across the whole header/body.
Parsing does not authenticate a peer or imply valid routing/coordinate data.

CTest includes4252 one-stream original observations, packet ownership/dispatch/
skew/failure/cap tests and all eight dependency tests. Native packets also pass863
original serializer comparisons and1726 original constructor readbacks per
compiler, including256 fresh packet vectors. The guarded reference runs exact
original full PING constructor/serializer bodies through one byte stream over
the qualified original header/helper methods and frozen pure Vivaldi/BEncoder
classes. Only the setter's contact-array namespace is adapted; base wrappers
supply getters/header offsets, network/clock/IDs/providers and cached statistics.
It does not initialize a normal Java DHT transport. P06-12 records hashes/scope.

The next step is an isolated Qt6.12 UDP transaction actor using newly owned
numeric-loopback sockets and explicit owned-peer allowlists. Ordinary routing,
storage/persistence, property projection/endpoint policies, full plugin services
and runtime activation remain pending. This packet codec is not an operational
DHT, a complete plugin or a native client release.
