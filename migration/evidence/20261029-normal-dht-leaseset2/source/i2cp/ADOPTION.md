Apply six staged source leaves (three existing channel/test leaves, three new
LS2 library/test leaves), then append services/tests CMake.fragment sections
once. Source basis e124abb90ab846c4b057a6b868004c0acb737118; later metadata-only
commits may be associated only by exact beforeimage checks. No author compile,
Java, endpoint, router, profile, native-tree or shared-tracking mutation occurred.
This extends services.i2cp_initial_session / F06.i2cp_initial_session.

Explicit createLeaseSession consumes an actual ClientIdentity only when Create
is armed; no borrowed alias or unrequested private-key clone is used. The old
borrowed createSession API/tests keep their registration-only behavior.
reconfigureLeaseSession signs synchronously with that retained capability, so
callers need no duplicate key. First request waits within the caller-selected
absolute1..30000ms phase budget. Reconfiguration cannot extend that first-lease
wait. Failure/close destroys the signer, independent lease key and observations.

Finite original path: RequestVariableLeaseSet37 -> standard online LeaseSet2
netdbtype3 -> CreateLeaseSet2 message41. One default Enc0 independent key,
signing0/7 and1..16 nonempty actual leases. No LS1, meta/encrypted/offline,
multiple keys, externally supplied key options, LS properties/service records,
other encryption type, auth or TLS fallback. Existing unknown general session
options remain intact. dontPublish uses Java's case-insensitive exact true;
leaseSetType absent/3 or original-upgraded1 and EncType absent/0 are supported.
Version capability supports explicit numeric router>=0.9.38; absent/legacy or
unparsed version refuses this opt-in before consuming a key or sending identity.
Default/Host/public/router activation stays absent.

Lease keys use the exact I2P RFC3526 MODP2048 prime/generator2. OpenSSL secure
RNG generates a private exponent uniformly in2..p-2 and constant-time BN modular
exponentiation derives public g^x modp. Original KeyGenerator chooses226/2048
random bits; this native valid-exponent subset is not identical RNG/distribution
policy. No encryption primitive is implemented. Both private/public wire widths
are256-byte unsigned big endian. The Destination's original deliberately opaque
Enc0 bytes remain unrelated, never claimed as this independent pair. Renewals
reuse the generated lease pair for this owned session; restart generates anew.

Owned private material uses fixed arrays with OPENSSL_cleanse on move, clear and
destruction. The Create41 secret frame is fixed4096 bytes and cleansed after its
single socket write; output backpressure refuses a second secret frame instead
of unbounded buffering. Qt owns its internal plaintext loopback TCP write buffer;
this code cannot promise erasure of Qt/OS copies. The diagnostic decoder borrows
a caller-owned secret frame, whose owner must erase it. No keys are logged or
persisted. Original-object vectors contain only synthetic test keys.

Published time is max(now,lastRaw+1000), rounded to nearest second, exactly the
original handler/LS2 writer. Lease expiries floor to seconds. The header is the
latest expiry minus publication, uint16 seconds; type3 is inside the signature.
Bounds reject unknown session65535, zero tunnels, zero/expired leases, invalid
dates, expiry beyond uint32 seconds, negative/overflowing publication delta and
more than16 leases. Rejecting expired individual requested leases is a deliberate
safe subset: original copies them rather than filtering; none is fabricated.
Clock input comes only from this passive router date plus monotonic elapsed time,
not a global clock update. Full destination/id/current generation and supported
acknowledged policy bind every request; unsolicited/wrong/unsupported requests
withdraw this bounded channel. Renewal while an update awaits ack uses the last
acknowledged options. Publication/signing preparation is charged to the deadline.

LeaseSetSubmitted means signed Create41 bytes successfully queued to the actual
current socket. Original setLeaseSet()/OPEN also follows client signing+enqueue;
there is no protocol router acknowledgement of CreateLeaseSet. The passive
observation records revision/published/expiry/count/unpublished only, never a
private key. Notifications stay queued/current-only and allow reentry/deletion.
No OPEN/Connected/readiness/delivery/outbound payload rights are introduced.
The observation remains historical after expiry, and disappears on failure or
close. Lease health, delivery and original full client semantics remain open.

Meaningful affected tests: new pure-i2cp-leaseset2 (60s), extended existing
i2cp-initial-channel (60s), pure-i2cp-signed-session-config, pure-i2p-client-identity
and pure-i2p-datagram-codec. No old PASS is pooled as new qualification. Old channel
method bodies remain byte-exact. Tests include actual owned IPv4/6 requests,
both signers, retained-signing update, renewal pair reuse, fragmented wire,
unsupported/no-opt-in/wrong-ID/expired/tunnel controls, deadline/close withdrawal
and deletion from a public submission callback.

OFFLINE-COMMANDS.sh uses one javac and the already-built new native test ELF.
Actual RequestVariableLeaseSet/CreateLeaseSet2/LeaseSet2 objects from each pinned
official and modified JAR produce four1/16-lease0/7 cases, native verifies them,
then generates genuine independent test pairs and Java verifies native signatures,
exact leases/time/destination and public/private derivation. Changed signature is
refused. No random signature/key byte equality is claimed. Root should validate
the four exact row sets per direction, negative marker, exact file:/JAR class
origins and source/ELF before/after, within one180s budget. No security fence is
installed by this offline helper; JDK crypto provider loading is unrestricted
before any caller-added fence. Modified whole-core/source-fork equivalence stays
open; only actual executed finite interoperability may be credited later.
