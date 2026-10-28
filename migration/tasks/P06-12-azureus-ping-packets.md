# P06-12 Complete original Azureus PING packet assembly

Status: implemented bounded full PING codec, qualified locally; operational DHT/
runtime release integration remains pending. Compose qualified headers (P06-08), request options/upload
data (P06-09), reply positions (P06-10) and alternative contacts (P06-11) into
owned request/reply packets at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
Do this before the next owned-loopback Qt6.12 UDP transaction actor, which must
stay disconnected from the user profile and public networks.

Characterize exact original DHTUDPPacketRequestPing/ReplyPing constructor and
serializer bodies in one guarded stream, delegating their header constructors
to the already qualified exact original bodies. Include the original reply's
setAltContacts list policy (first16), version/network gates and optional upload
failure consumption after preceding fields. The reference may reuse immutable
qualified helper classes for primitive methods and actual frozen Vivaldi v1/
BEncoder classes; hash all dependencies and record namespace/base wrappers.
Original methods must remain identifiable byte-for-byte or have documented
namespace-only adaptation. Do not claim complete ordinary DHT initialization.

Supply immutable clock, contact/transport getters, network registration, IDs,
cached country stats, positions and canonical outgoing properties. Incoming
properties remain opaque: the lazy tolerant BDecoder/canonical projection and
endpoint interpretation are still pending. No CoreFactory/normal plugin/core/
transport, profile, DNS, real telemetry or network startup is allowed.

Native request/reply assembly must own the full packet and decoded fields, return
consumed total/header and original trailing bytes, preserve skew diagnostics on
later body failure and source provider reset offsets. Select known PING action
only, reject foreign/unknown actions before the corresponding header constructor
after the PRUDP request16/reply8 prefix, and test this dispatch separately from
primitive header parsing. Keep the original PRUDP8192-byte codec cap; future UDP
actor additionally enforces normal DHT PACKET_MAX_BYTES1400 and request/reply
connection-bit multiplexing. Failed partial outputs are never emitted.

Qualify complete request/reply source fixtures, malformed/truncated boundaries,
copy/move/input/tail ownership and source-built Java native packet readbacks.
Fresh GCC16/Clang24/ASan+UBSan+LSan must also replay the four dependency suites;
freeze exact source/helper/fixture/binary hashes and checkpoint/provenance.

Next: isolated numeric-loopback UDP actor with ephemeral owned sockets, explicit
owned peer allowlists, random connection/transaction IDs, matching endpoint/
action/network/transaction/connection, deadlines, bounded pending/datagram queues,
cancellation/destruction/reentrancy tests and no public plugin activation. Then
continue actual DHT routing/store services needed by ratings/message-sync and
the other plugins. Never confuse a PING transport proof with an operational DHT.

Result:4252 one-stream packet observations plus all four primitive suites pass
on fresh GCC16/Clang24/ASan+UBSan+LSan. Ten CTest rows,863 packet encoder
comparisons/1726 Java packet readbacks plus every primitive Java readback suite
pass per compiler. Evidence: migration/evidence/P06-12-azureus-packets. Continue
the isolated Qt6.12 UDP actor without a plugin/profile/public-network activation.
