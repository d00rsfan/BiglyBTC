# P06-13 Owned-loopback Qt6.12 Azureus PING transactions

Status: implemented bounded owned-loopback actor, qualified locally. Operational
DHT/host integration remains pending. Continue after qualified PING assembly P06-12.
Implement a bounded Qt6.12/C++23 actor for newly owned numeric-loopback UDP
sockets and explicitly registered owned peers only. No public routes, DNS, proxy,
discovery, router mapping, existing application socket or user profile is allowed.
Bind only a fresh ephemeral port. The running Java BiglyBT is untouched.

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f: inspect original PRUDP
support/request, DHT packet handler and source packet action/1400-byte constants.
Source original framing classifies request/reply by connection/action high bit;
pending transactions/network filtering are relevant. Do not call this actor a
port of the entire original scheduler/transport: retries, priority/send queues,
tunnels, proxy/binding/privacy policy, statistics and ordinary service lifecycle
remain pending. Explicit endpoint/connection validation and bounded queues are
native probe policy; no full integrated baseline is advanced around them.

Use the qualified packet codec for all input/output. Generate fresh high-bit-set
connection IDs and collision-checked transaction IDs, preserve the advertised
originator-version reply negotiation, and match replies to owned endpoint,
network, PING action, transaction and connection. Enforce1400-byte datagrams
before allocation/parse, bounded pending operations/peer lists and limited work
per event turn with at most one drain continuation. No malformed/foreign packet
may retire a legitimate pending transaction.

Use monotonic deadlines (Qt QChronoTimer/C++ chrono), explicit cancellation,
same-owner-thread callbacks, QPointer receiver lifetime and move-only callbacks.
Remove/disconnect a pending record before invoking its callback. Use a bounded
completion deque with one coalesced owner-thread delivery event; capacity includes
pending and queued completions. Deliver outside the UDP socket event stack.
Handle reentrant
cancel/new requests/close/destruction and callback exceptions safely. Destruction
must emit no callbacks; close/target death must leave no pending timers or stale
receiver connections. Avoid unbounded queued completion lambdas.

Test fresh IPv4/IPv6 sockets, native peers and fenced source-built Java packet
peers, mismatched/spoofed/duplicate/malformed/oversized datagrams, deadlines,
pending/peer bounds, forced transaction collisions, cancellation, destroyed
receivers/actors and reentrant callbacks. Exercise a rejecting global proxy in
the owned test process with per-socket NoProxy. Java helper must use generated
home/profile, explicit source fence port allowlist and only its newly owned
DatagramSocket; never start a normal Java transport/core/plugin.

Fresh GCC16/Clang24/ASan+UBSan+LSan qualification, source/compiler/class/binary
hashes and immutable checkpoint/provenance remain required. Keep all existing
packet/component regressions. No routing/store/persistence/plugin activation or
complete operational DHT claim. After this proof continue the host DHT services
needed by ratings/message-sync and the remaining plugins.

Primary Qt API checks: [bounded datagram receive](https://doc.qt.io/qt-6.12/qudpsocket.html#receiveDatagram),
[chrono timer and owner-thread rules](https://doc.qt.io/qt-6/qchronotimer.html).
Oversized datagrams can be discarded with receiveDatagram(0). A precise timer
does not fire early, but may fire late under load; recheck steady deadlines and
make tests tolerate scheduler delay without accepting early expiry.

Result: all11 CTest rows and40 bidirectional source-Java/Qt exchanges over80
actual IPv4 datagrams per compiler pass on fresh GCC16/Clang24/ASan+UBSan+LSan.
Native Qt actor rows17/17 (15 scenarios/data plus init/cleanup), including IPv6;
no skip or suppression. See migration/evidence/P06-13-azureus-owned-udp. Initial
transaction/move evaluation-order failure is retained with its fix. Continue
node IDs/regular contacts and lookups before DHT routing/store/plugin activation.
