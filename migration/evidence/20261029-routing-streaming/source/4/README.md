Prepared source only. No compilation, Java/native subject, endpoint, router or
test has run. This is the actual retained modified `b0866aa8…` Streaming manager
interoperability fixture for Codec's separate native one-stream capsule.

`StreamingPeer.java` constructs the real `I2PSocketManagerFull` using a strict
test-only `I2PSession` adapter. The adapter exposes only the known online signing
key/Destination, one protocol6 listener, bounded receive, the inbound-server
PacketQueue send overload and teardown. Unexpected calls fail; it never connects
an I2CP session, performs lookup, supplies router delivery statuses, or starts a
router. Original manager accept/read/write/close/reset execute unchanged.

The pipe contract is **uint32 big-endian length followed by the exact I2CP mux
gzip payload**. Native Send5/Expires36 payloads pass unchanged to Java's original
DataHelper decompressor and actual manager listener/receiveMessage path. Actual
PacketQueue bytes pass through DataHelper's original compression policy, mux
ports/protocol and the same pipe framing in reverse. Stdout is exclusively wire
frames; original logging goes to stderr. The Qt fixture wraps real Java output
in Payload31 on an owned loopback I2CP connection. It does not implement an
application echo or invent Streaming ACKs/IDs/signatures/bytes.

The Java manager registers its original protocol6/port0 default listener because
the original manager's Properties constructor does not set ports. The adapter
permits only actual port9000→80 traffic, and verifies reverse target/ports.
The actual accepted socket must report peer9000/local80 and the exact native
Destination. Synthetic DSA0 native and Ed7 Java keys come from the retained
ordinary public test-key fixtures; native LS2 uses its existing independent key.

One explicit QTest target has five cases:

- Real HTTP-shaped request, exact 8192-byte deterministic multi-packet response,
  native FIN, real peer EOF/close and exact native byte sequence.
- Loss/reorder: drop the first actual native data packet; require its genuine
  same-identity retransmission, forward a duplicate, and reorder the second/third
  real Java response packets. Java's supported initial-window4 option makes
  concurrent response packets available in this row only. No protocol byte is
  rewritten. Require exact application bytes and native retransmission evidence.
- Atomic64KiB+1 native TX refusal with unchanged queue, then a real96KiB HTTP
  response while native application reads are held. Require an actual signed
  Delay61000 choke, bounded near-capacity RX and a rejected overflow packet.
  Resume4096-byte reads, require actual signed Delay0 unchoke and the exact whole
  response/EOF. This is protocol backpressure, not a kernel-stall measurement.
- Actual Java `I2PSocket.reset()` must produce native `RemoteReset` and withdraw
  read/write authority.
- Native local cancellation after a real Java data packet is held. Release real
  late peer packets, then perform genuine signed reconfigure/UPDATED on the same
  FIFO I2CP stream. Read/write must still refuse and operation/Cancelled remain
  unchanged after that positive consumption fence. No remote cancellation status
  is fabricated or inferred from a timeout.

The adapter bounds packets32768, mux40960, outgoing16, incoming1, lifetime4096.
The Qt bridge bounds its pipe/read/write buffers and held late packets16. Each
fresh helper uses at most128MiB heap; one stream/one Java process at a time.
Each native operation has60s absolute budget and the explicit CTest budget is
480s for all five rows. Public/application backend, lookup/tunnels, Streaming
listener activation, offline signatures, modified whole-source equivalence and
general TCP/HTTP/BitTorrent integration remain open. A separate real-router positive in `real-router/` reuses the already proven
isolation/router foundation. The controlled adapter rows retain their narrower
packet-routing meaning and do not earn real-router credit.

Root commands after source adoption and compiler availability (no author runs):

```bash
PEER=/home/yurii/Java/BiglyBT/build/i2p-streaming-java-peer-20261029
JAR=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
CLASSES="$PEER/classes-r1"
mkdir -m 700 "$CLASSES"
nice -n 5 /usr/bin/javac -J-Xmx128m --release 17 -cp "$JAR" \
  -d "$CLASSES" "$PEER/StreamingPeer.java" >"$PEER/javac-r1.log" 2>&1
# Stop on javac failure. Record the actual command/exit, JDK binary/version and
# the TWO resulting class SHA256s. Reuse this one image across native lanes.
cmake -S /home/yurii/Java/BiglyBTC/preview/native -B ACTUAL_NATIVE_CACHE \
  -DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=ON \
  -DBIGLYBTC_I2P_STREAMING_JAVA=/usr/bin/java \
  -DBIGLYBTC_I2P_STREAMING_JAR="$JAR" \
  -DBIGLYBTC_I2P_STREAMING_CLASSES="$CLASSES"
nice -n 5 cmake --build ACTUAL_NATIVE_CACHE --parallel 1 \
  --target biglybtc-i2p-streaming-interop-tests
mkdir -p "$PEER/runs"
BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT="$PEER/runs/gcc-r1" \
  nice -n 5 ctest --test-dir ACTUAL_NATIVE_CACHE --parallel 1 \
  -R '^i2p-original-streaming-peer$' --output-on-failure \
  --output-junit "$PEER/gcc-r1.xml"
```

Root preserves existing cache/compiler/runtime flags and current source receipts;
the example only adds the explicit reference target options. CMake requires the
exact modified JAR and compiled classes. Ordinary builds need no Java dependency.
Once opted in, absent inputs/fresh output are failures, never skip credit.

Each fresh row retains actual Java command, full bounded stderr, Java application
and result receipts, raw produced/forwarded mux frames in both directions, and
bridge counters/controls. These packet files contain synthetic public test
application data/signatures, never CreateLeaseSet2 private-key frames. Root should
retain complete failure history and actual JUnit/ELF/class/source hashes. No new
collector, provider, dependency download, Host wiring or registry change is part
of this source capsule. `source-pins.json` distinguishes official source9134,
plugin33d and the actual modified class image; no fork-wide equality is claimed.
