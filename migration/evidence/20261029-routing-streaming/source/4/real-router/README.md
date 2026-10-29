Prepared source only. No new compiler, namespace, router or client has run.

This adds one positive real-router Streaming transfer to the controlled adapter
cases. It compiles the immutable parent `RouterTracker.java` with the new
`RouterStreaming.java`. Router creation, public-transport disablement, genuine
zero-hop/accepted-LS2 checks, warmed cache, retirement and router shutdown execute
the **unchanged parent methods**. The parent router foundation actually passed
under root's separate `i2cp-isolated-router-20261029/runs/r1`; this new Streaming
acceptance has no runtime credit yet.

The peer is a separate JVM using a real external I2PSession on the owned random
loopback I2CP port and an actual I2PSocketManagerFull. No Proxy/session adapter is
used in this positive. Two fresh Ed7 Destinations/keys are generated inside the
fence. Native imports only its own key file and constructs its existing genuine
LS2. The original manager admits only the exact native Destination. Native uses
the complete peer Destination, protocol6/ports9000→80, request+FIN, and verifies
the exact HTTP-shaped8192-byte response and peer EOF. A real Java application
completion marker additionally proves original output-close returned before the
native TCP is retired. No Streaming status/application response is fabricated.
The native general datagram receiver stays disabled.

The unchanged fixture still explicitly copies the current, accepted,
signature-verified peer LS2 through the public router store into the native
client subDB. Both genuine zero-hop clients are checked first. This is a warm
local routing precondition; **no lookup/discovery/public/multihop claim** follows.
The old `tracker.*` leaf names are reused only to keep the controller unchanged;
their new contents identify the actual Streaming peer. The router controller's
historical scope label is retained; the outer result labels the new operation.

`run.py` is a focused derivative of the parent runner. Its hard `fence()` body
is AST-identical: new user/network/PID/IPC/UTS namespaces, only lo, no capabilities
or routes, nested user namespaces disabled, no host profiles/sockets, pre/post
unreachable controls and owned-PID-only cleanup. It preserves the exact mounted
runtime set and330s outer bound. There is no fallback if isolation fails. The
two Java heaps are bounded256MiB each; native budget110s, stream budget60s.
Only the new peer/probe/class whitelist, ninth Streaming archive, native source
set and the planned root `PASS_ROUTING_STREAMING_COHORT` result gate differ.
The actual future source CID, result and native archive hashes are required;
no hypothetical receipt passes source preparation.

Root-only commands after the ordinary Streaming cohort passes (one compile
slot; preserve the actual compiler/Qt cache flags):

```bash
H=/home/yurii/Java/BiglyBT/build/i2p-streaming-java-peer-20261029/real-router
T=/home/yurii/Java/BiglyBT/build/i2cp-isolated-router-20261029
JAR=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
# Use root's actual Java/Javac pair compatible with retained b086 (the proven
# router run used JDK26). Record exact versions/binary hashes and both commands.
mkdir -m700 "$H/classes-r1"
nice -n 5 "$JAVAC" -J-Xmx128m --release 17 -cp "$JAR" \
  -d "$H/classes-r1" "$T/RouterTracker.java" "$H/RouterStreaming.java" \
  >"$H/javac-r1.log" 2>&1
# Stop on failure. Exactly RouterTracker{,$Tracker}.class and
# RouterStreaming.class are required; no unrelated fixture classes on classpath.
cmake -S "$H" -B "$H/probe-build-r1" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$ACTUAL_GCC" \
  -DQt6_DIR="$ACTUAL_QT6_DIR" \
  -DBIGLYBTC_NATIVE_SOURCE=/home/yurii/Java/BiglyBTC/preview/native \
  -DBIGLYBTC_NATIVE_BUILD="$ACTUAL_PASSED_GCC_CACHE"
nice -n 5 cmake --build "$H/probe-build-r1" --parallel 1
nice -n 5 python3 "$H/run.py" \
  --native-result "$ACTUAL_GCC_RESULT" --source-commit "$ACTUAL_SOURCE_CID" \
  --probe-build "$H/probe-build-r1" --classes "$H/classes-r1" \
  --jar "$JAR" --java "$JAVA" --output "$H/runs/r1" \
  --acknowledge-owned-signed-cache-precondition
```

No default CTest/Host/backend/router
activation is added. Only the tiny standalone probe links the nine current GCC
archives; no native core/provider/Qt rebuild or old tracker replay is requested.
Success requires all three own processes exit0, exact native/Java application
receipts, actual client retirements, no public transports, and unchanged
source/JAR/classes/probe/archives after execution. Preserve failures and logs;
the native.keys file is removed by the existing runner finally block. Generated
router key material remains private in the owned run and must not be published.
