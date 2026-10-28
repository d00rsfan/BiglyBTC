# Owned-loopback Azureus UDP actor review

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; original PRUDP high-bit
demux, transaction/request handling, DHT network filtering and1400-byte normal
packet limit were inspected. Packet behavior delegates to the exact qualified
P06-12 codec. This actor is a new native bounded probe policy, not a port of all
original scheduler/retry/priority/tunnel/proxy/binding/statistics behavior.
Endpoint/connection matching and pending retention on malformed/foreign packets
are explicit native policy. No full source integration baseline is advanced.

The actor binds only a fresh ephemeral numeric-loopback socket, with per-socket
NoProxy and an explicit owned-peer allowlist. It has no public-mode option,
profile access, discovery/router mapping, DNS, Tor/I2P process or ordinary Java
core/transport/plugin startup. Tests never inspect or signal a running program.
Spoof test senders are also newly owned sockets, simply absent from the actor's
allowlist. Replies route to the actual owned UDP sender, not header metadata.

Pending+queued completions share a cap<=256; peer lists are separately bounded.
One coalesced completion event delivers outside the socket stack, at most64 rows
per turn. Datagram draining similarly yields after64 and queues at most one
continuation. Oversized datagrams are discarded before body allocation. Fresh
Qt system random words supply connection IDs/high bit and collision-checked
transactions. Trusted test injection proves bounded collision/random failure.
Monotonic deadlines are checked on both timer expiry and reply handling. Precise
Qt timers can fire late; no real-time guarantee is made. Callback receivers are
guarded, pending entries/connections are removed first, and destruction drops
callbacks. Cancellation/close are queued under the same capacity cap; already
completed values retain their result. Source provider/statistics state remains
supplied, with no actual routing/service data advertised as operational.

The first development Qt run had8 passing/9 failing rows: moving the decoded
reply into finish's second argument could clear the referenced header before
evaluating transaction in its first argument. The scalar transaction is now read
before the move, independent of C++ argument evaluation order. Original failure
logs/source hash are retained under before-transaction-move-fix. The complete
development suite then passed. An initial compilation also required checked
conversion of Qt's int senderPort to quint16; fixed before qualification.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan pass all11 CTest rows. The Qt
row has17 passes (15 scenario/data rows plus init/cleanup), zero failures/skips.
These cover IPv4/IPv6/native peers and originator-version upgrade, required
network/action/transaction/connection/endpoint bindings, duplicates, empty/
truncated/oversized packets, strict deadlines/late replies, operation/queue/peer
limits, cancellation, target/actor death, reentry/close/throwing callbacks,
collision/random failure, global-proxy bypass and bounded drain. All ten PING/
primitive CTest rows also pass on unchanged qualified lower inputs.

Each compiler additionally passes40 actual IPv4 request/reply exchanges over80
datagrams across five network/version cases:20 native requests and20 Java requests.
The Java peer runs exact source-qualified full packet constructors/serializers,
with explicit clock/network/ID/provider context and unchanged frozen pure classes.
An unchanged ReferenceFence restricts each JVM to its newly owned native port;
the Java DatagramSocket is connected to that same endpoint. Both directions
validate matching IDs/network/negotiation. Cached peer version51 upgrading to
advertised55 is included. No normal Java DHT service is started. Java IPv6 socket
interoperability is not claimed; native IPv6 and prior Java IPv6 wire fixtures
are separate coverage.

Exact85 compiled/tool/fixture inputs, source baseline, nested helper/core classes,
Qt6.12 Core/Network/Test runtime hashes, commands, logs and binaries are recorded.
Leak detection remains enabled, LSAN_OPTIONS is cleared and there are no
suppressions. The final matrix has no sanitizer failures. No independent reviewer
was used. ReferenceFence and original source HEAD/index remain unchanged.

Ordinary transport scheduling/privacy, persistent identity/contact maps,
coordinate/provider lifecycle, lazy property projection, DHT lookup/routing/
anti-spoof/store/persistence and dependent plugin/host activation are pending.
The running original client and its profile were untouched. This is an isolated
transport proof, not an operational DHT or a native release/performance claim.
Immutable implementation hashes follow in a later provenance commit.
