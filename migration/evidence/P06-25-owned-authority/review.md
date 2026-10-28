# Owned FIND_NODE authority and STORE admission review

The generated Qt UDP service now connects the qualified P21 private Authority
and P22 Admission inside one owner. Previously the development server could
return inert reply snapshots; those snapshots could not authorize a STORE. The
new explicit service mode creates its own CSPRNG-backed authority after binding
a fresh numeric loopback socket, derives the local identity from the actual
endpoint and version, and constructs admission on the actual Qt owner. Startup
failure clears the socket, node cache and partially created services before
publishing Bound. Generated service configuration is mutually exclusive with
the existing inert reply modes.

An empty public FIND_NODE target issues a token from that same authority. The
fixed 20-byte lookup API remains available; configured contacts are selected only
for the full target. The caller owns token use; no token/contact persistence is
added. STORE invokes the real authority verifier only at Admission's Verify
gate. Empty requests, key/value-set mismatch and unseeded forwarded requests
therefore retain the original cold-cache behavior. Successful token verification
does not manufacture key ownership: exact-byte supplied metadata still controls
existing/closest/block decisions, and unknown metadata grants no storage rights.

The transport compares actual sender IP with advertised IP before sender-ID,
authority or admission work. A mismatch returns ERROR1 containing the observed
transport endpoint. Same-IP advertised-port changes remain permitted; sender ID
uses the advertised endpoint and originatorVersion, while authority cookies use
IP without port. Negotiated packet version and originatorVersion remain distinct.
The implementation preserves ordered admission events, nullable diversification
and block fields, ERROR2 serialization and the original older-than-14 fallback.
The current owned actor supports versions 51-55, so that older branch is not
exercised through this actor. Encoding failures and replies larger than 1400
bytes send no partial datagram. No signature/trust decision is inferred from
merely carrying a block request/signature.

Receipts own their FIND_NODE/STORE packet, sender contact, policy result/error,
and all bytes referenced by event indices. The bounded pull queue reserves room
before sender identity, authority or admission work; a full queue drops the
request before those effects. Whole packet decoding happens earlier and can
already affect the separate qualified node cache. Pulling and retaining a moved
or copied receipt does not retain borrowed packet/config views; queued receipts
remain available after close, while live private service state is destroyed.
The owner-only state snapshot exposes counters and local ID, not authority keys.

Existing asynchronous public operations retain endpoint/transaction/connection/
network/action binding, fixed deadlines, shared pending/completion capacity and
Qt receiver lifetime guards. Detailed callbacks own either a normal reply or a
structured ERROR; old wrappers retain their established Remote failure mapping.
Service admission is synchronous and exposes observations through owned pull
receipts, without introducing a host callback during the policy gate. Alive and
store events are intentions recorded by this generated mode; they do not mutate
a router, normal contact history or database.

## Independent source evidence and socket comparison

The source baseline is `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Original control/transport/contact methods and the independently generated Java
bridge are pinned by full file/method hashes. The source-only corpus froze 1070
observations before native comparison, including 452 STORE requests, 110 token
queries and 452 serializer attempts. Its manifest SHA256 remains
`944ba39709a5a1b32c6e9799088351e5b99735e47ea6e3e918c8d911939ab99f`.
These observations characterize the source bridge; this milestone does not claim
a native replay of all 1070 rows. The targeted socket receipt checks below are
separate evidence with their own counts.

Each source/native authority has an independent key. Native production authority
uses private CSPRNG material; Java's generated source peer uses its own test key.
The harness never substitutes an already-computed authorization boolean or
requires unrelated authority instances to emit equal numeric cookies. For each
reverse request, a separate source controller replaces only the four-byte STORE
cookie with its own previously issued token, or that token xor 1 for the invalid
case. It then executes the original source verification at the original policy
gate. A source prediction is emitted before the actual native request is sent.
The prediction and actual request must otherwise have identical bytes.

After the native reply, Java waits for NEXT. The harness pulls exactly one native
owned receipt and current authority/admission counters, compares them with the
independent source prediction, then releases the next request. This checks event
order, gate validity, digest counts before/after verification, IP cache entries,
sender identity and rejection memo at each operation boundary. The FIND_NODE
cookie is opaque across independent instances: only its four reply bytes/token
label are normalized. Other reply body bytes and all gate/state observations
must agree. The forward source controller also executes actual original
authorization, retaining its receipts and proving the cold/warm gate sequence.

The completed matrix combines five protocol/network contexts with four immutable
service snapshots: unseeded, default, metadata and block. It passes 210 requests
in each direction, 840 actual IPv4 datagrams and 210 reverse owned-receipt/gate
comparisons per compiler run. Of the 210 forward operations, 170 use public actor
APIs and 40 use a separate owned raw UDP socket solely for advertised-IP/port
header probes. The raw path checks exact peer, transaction, full connection,
network, supported action/version, size and fixed deadline before normal body
decoding. Ordinary operations and every reverse request remain on the actor.

Java uses two newly created connected loopback sockets: primary for ordinary and
reverse traffic, secondary for the two raw cases. The unchanged ReferenceFence
permits exactly the two fresh native ports. There is no wildcard/DNS/listener
scan or existing daemon attachment. Request and reply wire versions, originator
versions, connection/transaction binding, callback counts and terminal counters
are checked explicitly. Each case uses only its exact created Popen handles for
cleanup, limits the JVM CPU count to two, and persists its completed or failed
transcripts under the recovery tree before advancing.

## Native regression scope and development corrections

The new Qt suite passes 30 rows including init/cleanup. It covers IPv4/IPv6
public token-to-STORE flow, full versus empty lookup contacts, changed advertised
port with a distinct packet/originator version, observed-IP rejection, cold
policy gates, bad token, unknown metadata, block/null/empty serialization,
bounded receipt backpressure and retained ownership, configuration bounds and
exclusivity, Qt owner movement and wrong-thread access, and startup rollback.
The configured snapshot is copied into the actor and caller-owned contact/key
vectors are then cleared to check isolation. Config itself remains noncopyable;
the test copies the supplied GeneratedStoreService snapshot before moving Config.

Provider fault rows distinguish cold sender SHA1 failure from identity-warm but
authority-cold MD5 failure. The former uses a provably distinct restricted node-ID
key and an empty value set, requiring no sender/alive/admission/auth work or
reply, then checks recovery on the same actor after restoring the provider.
Provider properties are scoped and restored. Native crypto failure remains
P21's explicit fail-closed policy; it is not a claim to reproduce the original
source's catch-and-zero crypto failure behavior.

Development failures are retained under
`migration/evidence/P06-25-owned-authority/development/`:

- `before-expected-variant-fix/` preserves the first failed compile and driver.
  Both expected::emplace calls were changed to assignment from an explicitly
  constructed normal/ERROR variant, because emplace requires a nonthrowing
  value constructor. The affected raw receive loop was line-broken for review;
  token projection verifies only those two semantic substitutions.
- `before-snapshot-move-fix/` preserves the subsequent strict GCC Release
  diagnostic while inlining a move of Config with a disengaged new optional
  service. GeneratedStoreService's default/copy/move special members were moved
  out of line, without changing defaults, supported fields or strict warnings.
- `before-test-name-fix/` preserves the next test compile failure. The new lookup
  isolation case now copies the supplied service snapshot rather than the
  noncopyable Config, and the test helper is named hasEvent to avoid hiding by
  QObject::event. Assertions and the expected 30-row total remain unchanged.
  The aborted preliminary edit and its extra build log remain development
  history, not qualification evidence.

Java helper relocation has a separate proof: the original harness bytes,
source-material/qualified-reference/collector command ASTs and all 27 generated
class hashes remain equal to the frozen v2 reference. Persistent reference and
case copies support reboot recovery; they do not rewrite frozen fixture outputs
or expand the unchanged source fence.

## Completed qualification

Three originally fresh persistent GCC16 Release, Clang24 Release and Clang24
ASan+UBSan+LSan Debug builds pass all31 combined CTest rows and103 Qt checks
per compiler, with no failures/skips or warning/sanitizer suppressions. The new
suite contributes30 Qt passes; unchanged owned suites contribute73. All six
prior packet/value/node readback suites pass. Actual authority traffic adds840
datagrams and210 reverse receipt/gate comparisons per compiler; old STORE/ERROR,
FIND_VALUE, FIND_NODE and PING bring the total to1600 datagrams per compiler.
Across the three lanes this is4800 datagrams and630 new gate comparisons.

The original230 lower input inventory permits changes only to the four declared
actor integration files; the other226 and two quota-recovery tools stay unchanged.
All246 qualification inputs,44 helper roots/548 class hashes, original core
digest, exact runtime/reference files, commands/logs/reports and compiler binaries
are guarded. Original Java HEAD/index are unchanged. The matrix records progress
before commands and copies the exact fenced /tmp runtime namespace after each
command, including failure; compiler roots and draft sources live persistently.

Development interoperability separately found the same incorrect one-byte
expectation in both new test driver and Java peer for default scenario9's two
keys. Original Java SOURCE_EXPECT and actual replies both contained two DT_NONE
bytes. Only those validators changed; source controller and1070 goldens did not.
The initial relocation proof preserves all27 v2 class hashes. Subsequent peer v3
changes only its main validator class; a fresh peer-only build and full helper
build prove all26 other classes, including controller, fence and Served, unchanged.
The old peer/driver, failing logs, exact transcripts, baseline map and new class
proof remain retained. No expected source output was regenerated or normalized
to conceal that failure. A fresh development run then passed all20 contexts.

Three user-authorized workers split runtime, exact source and interop work;
root integrated, reviewed and ran qualification. Independent scoped source,
runtime, matrix and peer-fix reviews are retained. This is not a separate full
client parity review. The first fresh matrix passed GCC tests and socket traffic, then hit the /tmp
user quota while writing a readback transcript. Its271 generated files/31MiB and
all top-level reports/logs were copied persistently. A manifest covers203 old
reconstructible compiler objects/221228576 bytes removed from ten exact completed
build roots;150 retained executables/libraries and caches were hash-verified.
A narrowly validated recovery wrapper reuses only the originally fresh GCC root
and replays its complete commands, followed by fresh Clang and sanitizer roots.
It preserves all246 inputs, flags, sanitizer settings and test assertions; GCC
build commands are incremental no-ops. Failed evidence and wrapper provenance
remain separate from the successful final matrix.

This milestone does not implement a production database/map, replacement/expiry/
republish policy, routing or bootstrap, persistent contact/token storage, logical
STORE batching, key-block trust, application/plugin/UI activation, profile
migration, public networking or a complete native DHT. It makes no performance
claim and does not interact with the running Java program or live profile.
