# Explicit outbound I2P Streaming — source only

Eight native source leaves against exact unchanged I2CP bytes at native
`4b2f4b5748d8676d3436b174b94805148453cdd4`, plus append-only services/tests CMake
sections. No native checkout, profile, provider, router, network, compiler or
original subject was changed/run by the author. The separate actual modified
Java-manager acceptance capsule is authored by Oracle; adopt and qualify both
together. A pure codec PASS alone cannot qualify a usable connection.

`InitialChannel::connectStream(fullDestination, StreamOptions)` adds one explicit
outbound byte stream over the already-owned loopback I2CP TCP channel. Options
require nonzero local/remote ports and one absolute1..120000ms operation budget.
The full Destination is validated and self-connect refused; this corresponds to
the original manager.connect(Destination), after caller resolution. There is no
implicit address-book, DNS or hash lookup. The operation requires the retained
owned signing capability, actual same session/full local Destination and genuine
unexpired LS2 enqueue; CREATED by itself remains insufficient. Existing general
datagram19/20 and tracker RAW18 decoders/permissions remain strict. Protocol6 has
separate gzip/packet entry points and consumes only the current operation's data.

Public calls accept bounded output, take owned input, finish output, locally
cancel and inspect the coalesced current observation. No application callback
runs in the socket notification stack: QPointer/generation queued notifications
allow deletion, close and restart. Socket loss/close clears streams before owned
identity cleanup. Failed/expired/cancelled operations cannot read/write or
reconfigure other work; reconfigure is allowed once a stream is inactive. Local
cancel makes no remote reset/acknowledgement claim. Operation/local stream IDs
never repeat within this object's1024-operation lifetime, including channel
restart. Exhaustion is a refusal, not an automatic fresh identity/connection.

## Original wire and conservative native envelope

Exact official I2P `9134f808337b401e8e53c73734c81fab04280c9d` source is retained
with tree/blob/SHA256 verification; see source-pins.json. Packet.java and
PacketLocal.writeSignedPacket define the22-byte header, NACK words, option order,
zeroed-signature preimage, online DSA0/Ed7 signatures and32768-byte payload bound.
The eight initial SYN NACK words carry the selected peer Destination hash, as in
ConnectionDataReceiver. SYN/FROM binds the exact full resolved peer; SYN, CLOSE,
RESET and any explicitly signed packet require that peer's valid signature.
Established unsigned data is accepted under original stream-ID context; do not
call it individually authenticated. Offline/ECHO/unknown flags/options/crypto
are unsupported. The codec deliberately refuses extra option bytes instead of
guessing their signature layout.

ConnectionOptions supplies initial RTO9000, first/steady RTT formulas, RTT cap60000
and RTO clamp100..45000. Connection.RetransmitEvent doubles timer RTO, preserves
sequence IDs and limits eight retransmissions. PacketQueue expires timed I2CP
packets25ms before that retry, minimum25ms; final CLOSE/RESET skip LeaseSet, and
nonce0 remains untracked router submission. Native retry timers begin after
actual successful socket enqueue; local enqueue, router acceptance, peer ACK
and application receipt are distinct. Untimed ACKs use ordinary Send5. Original
tag-count/status-listener tuning is not reproduced by this bounded sender.

This first native envelope keeps one outstanding packet/window1, sends signed
packets and immediate ACKs, and uses original MSS1730 with negotiated minimum512.
It does not claim adaptive congestion/window/fast-retransmit/performance parity.
RX accepts bounded out-of-order packets, reports missing sequence IDs, suppresses
duplicates and delivers contiguous bytes. Signed ordered CLOSE preserves its
final payload and reports EOF; both close directions plus matching local FIN ACK
produce Closed. Closed context can ACK duplicate peer CLOSE while retained and current;
the original automatic five-minute TIME-WAIT scheduler is not ported. Explicit
new operations use lifetime-unique local IDs and cannot admit old stream bytes.

TX64KiB includes in-flight unacknowledged payload, and RX64KiB includes ready and
out-of-order payload, with at most64 reorder entries. Container/header metadata
and bounded codec/signature/transient buffers are additional. A refused TX write
does not partially enqueue; RX overflow is not acknowledged and emits original
delay61000 choke. Reading releases it with delay0. There is one pending reliable
packet, one coalesced ACK, one bounded Qt write and a fixed4KiB lifetime-ID table.
Stalled socket buffering keeps operation/LS2 deadlines active without1ms polling.
The operation budget is stricter than the original independent connection/read/
write timeouts; it is an explicit bounded API, not default policy equivalence.

## Composition and actual qualification still required

Append CMake.fragment sections once. New pure target/suite:
`biglybtc-i2p-streaming-tests` / `pure-i2p-streaming` (30s). Source tests cover both
signing types, signature/identity/header/options/CRC/concatenation refusals,
strict protocol separation, ordered/reordered/duplicate bytes, actual64KiB
capacity/choke/unchoke, dispatch-based retry boundaries/reset and bidirectional
signed close. Existing test bodies are untouched. Run the affected I2CP channel,
datagram/receive/tracker regression union, not unrelated Java/proxy/UI history.

The actual Java-manager acceptance must route real protocol6 packets through an
explicit I2PSession adapter and the owned native I2CP fixture: original signed
handshake, HTTP-shaped request, multi-packet response, real loss/reorder/retry,
reset and cancel/late-packet fences. No substituted application echo. The adapter
is a declared test transport, not real router/tunnel/Internet interoperability.
Require its explicit compiled helper/JAR pins, actual class origins and positive
runtime results before credit. Root owns compilation, subjects and adoption.

Modified JAR `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`
is separately attributed; equality with official source is not asserted. Original
plugin `33d97eca67a9f332e3d4da7e7492ec54f4bec078` uses the same manager/full-peer
boundary with its own timeout/options and key ownership. No embedded Java,
existing-daemon adoption, SAM replacement, public/default/Host activation,
Streaming listener, saved tracker/BT peer/payload permission or rank injection.
Native Enc0 LS2 is narrower than original plugin default Enc4; remaining keys,
router auth/TLS, tunnels, public transport, general socket policy and full P07
compatibility remain open. Existing component stays partial/in_progress.
