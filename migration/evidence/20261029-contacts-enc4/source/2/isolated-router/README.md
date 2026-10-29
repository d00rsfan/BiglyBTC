Source only. This is a fresh derivative of the immutable, actually passed Enc0
router foundation. No Enc4 router/compiler/native/Java subject has run yet.

The same hard bwrap fence is retained verbatim: fresh user/network/PID namespace,
lo only, no capabilities/routes/host profile/socket mounts, pre/post unreachable
controls, fixed owned configuration and only owned-process termination. No flags
alone constitute isolation. The modified JAR is the same pinnedb086 artifact.

Only the selected crypto changes: native and Java set leaseSetEncType4, and the
router's actual accepted/current signed LS2 check requires one Enc4/32-byte key
for BOTH clients. Their Destinations remain the unchanged Enc0 online format.
The router's normal Create41 handler derives and validates the private/public
pair. The explicit cache precondition still copies only the actual accepted and
verified peer LS2 through public router store, after both zero-hop clients exist.
No discovery, remote ECIES/ratchet, public tunnel or general receiver credit.

The native probe still performs actual lookup38, signed2 connect, claimed3
scrape and correlated RAW18 counts3/4/5, followed by actual retirement. The actor
is not used here. Its ninth static archive is needed only because the current
shared InitialChannel object contains Streaming references. No core/provider
rebuild is part of this small standalone acceptance build.

After root's ordinary Enc4 GCC cohort passes, use a fresh Java classes directory
and build directory. Substitute actual source/result/cache/compiler/Qt values;
the runner requires `PASS_ENC4_LEASESET_COHORT`, source/images unchanged and the
matching source-before map. No future source or binary hashes are assumed.

```bash
H=/home/yurii/Java/BiglyBT/build/i2cp-enc4-20261029/isolated-router
JAR=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
mkdir -m 700 "$H/classes-r1"
nice -n 5 "$JAVAC" -J-Xmx128m --release 17 -cp "$JAR" \
  -d "$H/classes-r1" "$H/RouterTracker.java" >"$H/javac-r1.log" 2>&1
cmake -S "$H" -B "$H/probe-build-r1" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$ACTUAL_GCC" \
  -DQt6_DIR="$ACTUAL_QT6_DIR" \
  -DBIGLYBTC_NATIVE_SOURCE=/home/yurii/Java/BiglyBTC/preview/native \
  -DBIGLYBTC_NATIVE_BUILD="$ACTUAL_GCC_CACHE"
nice -n 5 cmake --build "$H/probe-build-r1" --parallel 1
nice -n 5 python3 "$H/run.py" --native-result "$ACTUAL_GCC_RESULT" \
  --source-commit "$ACTUAL_SOURCE_CID" --probe-build "$H/probe-build-r1" \
  --classes "$H/classes-r1" --jar "$JAR" --java "$JAVA" \
  --output "$H/runs/r1" --acknowledge-owned-signed-cache-precondition
```

Use root's actual compatible Java/Javac pair (the proven Enc0 router used JDK26).
Preserve all failures/logs/input/output hashes and exact ELF/archive relations.
No absence-of-input skip or fallback. Original330s outer/110s native/30s tracker
budgets remain unchanged, as do client retirements/zero public transports and
all three process-exit checks. Generated private router files must remain local;
the native key file is deleted by the existing runner's finally block.
