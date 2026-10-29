This is a prepared, **unexecuted** acceptance subject for the retained modified
`azi2pcore_2.13.0.1.jar` (`b0866aa8…`). It starts one real Java Router, one
separate original-JAR tracker client, and one native explicit channel. All three
run in the same fresh Linux network/PID/user namespaces. No router is installed,
no application backend is selected, and no native source or current dist changes.

The result deliberately covers **warmed local lookup and real same-router
forwarding**. The modified router creates a distinct client netDB per I2CP
connection. Hash38 searches the caller's DB; another local client's published
LS2 does not automatically become discoverable there. The controller therefore
waits for both actual registered zero-hop clients and actual accepted/current,
signature-verified LS2s, then uses the router's public `store` to copy that exact
tracker LS2 into the native client's DB. This single explicit fixture
precondition is separately recorded. It neither constructs a lease/status/reply
nor bypasses the native Hash38 request. Discovery, remote routing, public
tunnels, encryption in transit, Streaming, HTTP trackers/BT peers, Host/default
activation and whole modified-core equivalence remain unqualified.

The namespace is the outbound fence. `bwrap --unshare-net` exposes only fresh
loopback, with all capabilities dropped, nested user namespaces disabled, and
no host `/run`, `/tmp`, `/home`, sockets, or profiles mounted. The inner runner
checks the different namespace, only `lo`, routes, zero effective capabilities,
NoNewPrivs, and TCP/UDP documentation-address failures before starting any Java
or native subject. It repeats the fence after success. If namespace creation or
any fence check fails, **stop**: there is no unfenced/flags-only fallback.
Transport/reseed/NTP/UPnP/client-app configuration is an additional control;
the helper also checks the actual transport map and active-peer count.

The two Destinations are freshly produced by the actual Java `I2PClient` and
are distinct. The native client imports its bounded real signing capability,
signs/submits its independent LS2, performs Hash38 lookup, and sends real signed
Datagram2 BEP15 connect followed by claimed/unsigned Datagram3 scrape. The Java
peer verifies the signature/recipient, actual connection ID, two distinct
transactions, info hash and mux ports before emitting actual RAW18 replies.
Both subjects must independently observe exactly the expected exchange; native
counts are 3/4/5 and its general datagram receiver stays disabled. Java's tracker
is a separate JVM and explicitly refuses a RouterContext/internal-queue path.
Native close, original tracker destroy, actual router client retirement and
router shutdown are required. Only spawned Popen children are signalled.

Root runs this only **after** the current cohort joins and its GCC result is
PASS. Use the actual passed result and its source CID; no future CID or ELF hash
is guessed. The standalone project imports eight existing GCC archives and
compiles only `probe.cpp`. It does not configure/rebuild the native project or
provider. Java compilation follows it serially. Example commands (supply the
first two values, and choose a fresh build directory):

```bash
TASK=/home/yurii/Java/BiglyBT/build/i2cp-isolated-router-20261029
GCC_RESULT=/absolute/path/to/actual/passed/gcc/result.json
SOURCE=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["source_commit"])' "$GCC_RESULT")
NATIVE_BUILD=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["cache"])' "$GCC_RESULT")
PROBE_BUILD="$TASK/compiled-r1"
JAR=/home/yurii/Java/BiglyBT/build/i2p-modified-core-20261029/azi2pcore_2.13.0.1.jar
mkdir -m 700 "$PROBE_BUILD"
python3 -c 'import json,pathlib,sys;sys.path.insert(0,sys.argv[1]);import run;c=run.cache(pathlib.Path(sys.argv[2])/"CMakeCache.txt");print(c["CMAKE_CXX_COMPILER"]);print(c["Qt6_DIR"])' "$TASK" "$NATIVE_BUILD"
# Supply those exact two cache values below; do not select another SDK/compiler.
nice -n 5 cmake -S "$TASK" -B "$PROBE_BUILD" \
  -DCMAKE_CXX_COMPILER=/actual/cache/compiler \
  -DQt6_DIR=/actual/cache/Qt6_DIR \
  -DBIGLYBTC_NATIVE_SOURCE=/home/yurii/Java/BiglyBTC/preview/native \
  -DBIGLYBTC_NATIVE_BUILD="$NATIVE_BUILD" >"$PROBE_BUILD/configure.log" 2>&1
nice -n 5 cmake --build "$PROBE_BUILD" --parallel 1 >"$PROBE_BUILD/build.log" 2>&1
mkdir -m 700 "$PROBE_BUILD/classes"
/usr/bin/javac -version >"$PROBE_BUILD/javac-version.log" 2>&1
nice -n 5 /usr/bin/javac -J-Xmx128m --release 17 -cp "$JAR" \
  -d "$PROBE_BUILD/classes" "$TASK/RouterTracker.java" >"$PROBE_BUILD/javac.log" 2>&1
# Root records all three command exit codes; do not continue after a failure.
nice -n 5 python3 "$TASK/run.py" --native-result "$GCC_RESULT" \
  --source-commit "$SOURCE" --probe-build "$PROBE_BUILD" \
  --classes "$PROBE_BUILD/classes" --jar "$JAR" \
  --output "$TASK/runs/run-r1" --acknowledge-owned-signed-cache-precondition
```

One compiler job is sufficient; do not overlap javac with the probe compile.
The outer runtime budget is 330 seconds, with separate finite startup, native
operation, retirement and shutdown limits. At most two 256MiB Java heaps and
one small native probe run concurrently; allow approximately 0.8GiB RAM and
under 100MiB task disk beyond existing inputs. These are planning estimates,
not measurements. Namespace support/policy is an unexecuted prerequisite.

Inputs, full subject logs, actual commands/exits, fence receipts, cache
precondition, independent peer/native observations and retirement are retained
under the fresh run directory, including failures. `native.keys` is removed
after joining. The private router/client directories may contain synthetic
router secrets: keep them local and do not publish/archive those directories.
Only reviewed logs/receipts/public Destinations belong in public evidence.
The optional `CMake.fragment` is a future explicit target alternative, not a
default test: it would copy `probe.cpp` as `tests/i2cp_isolated_router_probe.cpp`.

No C++, javac, Java Router/session, native probe, namespace, socket, test or
installation was executed by the author. Preparation used source reads and
`javap` only; `source-pins.json` records exact modified-bytecode evidence and the
available original/plugin source relation.
