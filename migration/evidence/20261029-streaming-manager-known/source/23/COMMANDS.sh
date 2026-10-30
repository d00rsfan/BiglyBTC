#!/usr/bin/env bash
# Root-only future commands; author did not execute. Use the normal aggregate
# compiler budget. Compile this Java helper once, reuse exact class hashes in
# all native lanes; do not overwrite the qualified H helper/classes.
set -euo pipefail
: "${NATIVE_CACHE:?actual adopted-source cache required}"
: "${JAVA:?absolute reviewed Java executable required}"
: "${JAVAC:?absolute reviewed javac executable required}"
: "${OUTPUT:?fresh absolute owned output required}"
REFERENCE=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
SOURCE=/home/yurii/Java/BiglyBT/build/i2p-streaming-window-20261029/acceptance/StreamingPeer.java
case "$OUTPUT" in /*) ;; *) exit 2;; esac
case "$JAVA:$JAVAC:$NATIVE_CACHE" in /*:/*:/*) ;; *) exit 2;; esac
test ! -e "$OUTPUT"
mkdir -m 700 "$OUTPUT"
mkdir -m 700 "$OUTPUT/classes" "$OUTPUT/home" "$OUTPUT/tmp"
printf '%s  %s\n' b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 "$REFERENCE" | sha256sum -c -
# No Java router/session or networking is started by this helper compilation.
timeout --kill-after=5 180 env -u JAVA_TOOL_OPTIONS -u _JAVA_OPTIONS -u JDK_JAVA_OPTIONS -u CLASSPATH HOME="$OUTPUT/home" TMPDIR="$OUTPUT/tmp" \
  nice -n 5 "$JAVAC" -J-Xms16m -J-Xmx128m -J-XX:ActiveProcessorCount=1 -J-XX:+UseSerialGC \
  --release 17 -cp "$REFERENCE" -d "$OUTPUT/classes" "$SOURCE" >"$OUTPUT/javac.log" 2>&1
sha256sum "$OUTPUT/classes/StreamingPeer.class" "$OUTPUT/classes/StreamingPeer\$Adapter.class" >"$OUTPUT/classes.sha256"
# This reuses the ROOT's exact provider/Qt/toolchain/cache configuration. Native
# source adoption + actual source/image receipts are mandatory caller gates.
cmake -S /home/yurii/Java/BiglyBTC/preview/native -B "$NATIVE_CACHE" \
  -DBIGLYBTC_I2P_STREAMING_JAVA_ACCEPTANCE=ON \
  -DBIGLYBTC_I2P_STREAMING_JAVA="$JAVA" \
  -DBIGLYBTC_I2P_STREAMING_JAR="$REFERENCE" \
  -DBIGLYBTC_I2P_STREAMING_CLASSES="$OUTPUT/classes" >"$OUTPUT/configure.log" 2>&1
cmake --build "$NATIVE_CACHE" --target biglybtc-i2p-streaming-interop-tests -j 1 >"$OUTPUT/build.log" 2>&1
# Run after qualification owns all expected source/cache/image receipts. This
# is an explicit packet-session adapter on owned loopback, not a real router.
BIGLYBTC_I2P_STREAMING_ACCEPTANCE_ROOT="$OUTPUT/actual-peer" \
  ctest --test-dir "$NATIVE_CACHE" -R '^i2p-original-streaming-(peer|upload)$' \
    --output-on-failure --output-junit "$OUTPUT/tests.xml" >"$OUTPUT/ctest.log" 2>&1
sha256sum -c "$OUTPUT/classes.sha256"
