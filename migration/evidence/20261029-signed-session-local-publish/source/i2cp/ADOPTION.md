This source-only slice extends services.i2cp_initial_session / F06.i2cp_initial_session.
Apply the eight staged leaves (five existing, three new) and append the two
CMake.fragment sections once. No provider, Host, profile, router backend or
default activation changes. Parent channel451f and actual b998/82ef qualification
remain historical; new runtime/Java interoperability is pending root execution.

The pure signed-session module implements SessionConfig, Create1 and
Reconfigure2. The signature covers exact public Destination bytes, canonical
UTF-8 properties sorted by Java UTF-16 keys, then8-byte creation date. It does
not cover the I2CP framing or Reconfigure session ID. DSA0 SHA-1 raw r||s40 and
Ed7 raw64 reuse the existing OpenSSL implementations through two bounded raw
sign/verify entry points; the old datagram/identity algorithms remain unchanged.
No private key copies or new identity generation/persistence are introduced.
ClientIdentity is borrowed only for synchronous signing, never retained by the
channel. Empty/moved/cleared/wrong identities refuse before I/O.

The property limit remains original65535 encoded bytes, with255 encoded-byte
keys/values, unique keys and valid scalar UTF-8. Unknown options are retained.
Unsorted unique incoming properties are reserialized canonically for signature
verification, as original readProperties(false)/getBytes do. Invalid UTF-8 is an
explicit unsupported subset of Java replacement behavior. Exact13 client-only
options are filtered; >255 Java-unit strings are removed, while shorter strings
whose UTF-8 encoding exceeds255 bytes fail instead of silently disappearing.
The acknowledged merged option set also remains within65535 serialized bytes;
unbounded accumulation across updates is not supported.
Offline/transient-signing properties and any present i2cp.username/password
(even empty) are Unsupported. TLS/authentication-required begin refuses before
TCP; i2cp.SSL=true (Java case-insensitive exact true spelling, no trimming) also
refuses registration. There is no plaintext/no-auth fallback for those intents.
Neither codec projects original profile options/defaults automatically.

Creation milliseconds are explicit caller input, never an ambient/global clock
update. Null/negative/wrapped dates fail; freshness checks use the original
inclusive ±180000ms window, including failure for Java signed-window overflow.
The channel checks against its passive router date plus monotonic elapsed time.
This explicit per-attempt clock contract does not implement original automatic
Clock/BuildTime startup/skew adjustment and must not be described as that parity.
Ordinary SessionConfig parsing is a public DTO; it never grants admission.

createSession requires a current DateReceived connection. Only a current-socket
CREATED with real0..65534 ID advances to SessionCreated.65535 is original UNKNOWN.
Reconfigure requires exact same accepted public Destination and ID; only matching
UPDATED commits the putAll option merge. Omission retains old options. The
original router verifies create signature/date but intentionally does not
reverify reconfigure signature/date; native still signs correctly and never
labels UPDATED as router cryptographic verification. No request nonce exists:
one request is outstanding on the ordered TCP stream; unsolicited statuses
outside the matching phase fail. Partial/already-buffered incoming data and
pending output prevent arming a new request. This cannot authenticate a malicious
router; it is the original explicit loopback protocol contract.

Per-request absolute budget includes signing/serialization and acknowledgement
(the existing caller-selected1..30000ms budget). Preparation failure before I/O
leaves the previous phase intact. Timeout, cancellation, malformed/unknown
message, wrong ID, refusal or transport loss withdraw date/ID/acknowledged options
and close the connection before reuse. Public notifications remain queued,
coalesced, QPointer/generation guarded; an additional acknowledged revision guard
prevents nested event-loop reconfigure returning to the same state from emitting
an old Created notification. No public callback runs in a socket/timer frame.
The read cap/chunk/drain budgets remain131077/4096/16384 bytes. Only one bounded
config write is outstanding; pending options are not exposed as acknowledged.

SessionCreated means router registration only. Original connect() subsequently
waits for a LeaseSet/inbound tunnels before becoming usable. No Connected/ready,
tunnels, lookup, data, payload, persistence or backend rights are added here.
Future LeaseSet/other unsupported messages currently close this bounded channel;
it is not an operational full-router client. Automatic external-router selection,
TLS/auth, offline signatures, non-Enc0/signing types beyond0/7, subsessions,
LeaseSets and plugin integration remain separate work.

New target biglybtc-i2cp-session-config-tests / CTest pure-i2cp-signed-session-config
(60s); extend existing biglybtc-i2cp-initial-channel-tests / i2cp-initial-channel
(existing60s). Meaningful affected checks also include pure-i2p-datagram-codec
with retained actual Java vectors, pure-i2p-client-identity for the borrowed
owned-key contract, and pure-i2cp-initial-session-codec for shared framing.
No old PASS is counted as new qualification. Tests cover both signing types,
UTF-16/UTF-8 ordering, exact65535 limit, signature/date/options/opaque Destination
tampering, ID-outside-signature behavior, real owned v4/v6 Create/Updated and
merge, clear signer before ack, wrong ID/destination, UNKNOWN/refused statuses,
timeouts, cancellation/late data, partial unsolicited frame, nested real updated
reply and deletion from Created/Updated callbacks. No scheduling/allocation hook.

OFFLINE-COMMANDS.sh compiles the Java fixture once, then uses actual SessionConfig,
CreateSessionMessage and ReconfigureSessionMessage objects against each exact
pinned official/modified JAR, with an already-built current native test ELF.
Six configs × two messages =12 per reference/direction, plus auth refusal,
changed-signature refusal and a validly signed stale date. Random signatures are
cross-verified, not compared for equality. Canonical preimages/fields are exact.
Root should wrap the command in one180s offline budget, retain all logs/failures,
check class origins and current ELF/source before/after using the existing recipe
pattern. Inputs are existing public synthetic test signers; no private profile,
router/session.connect, downloads or endpoints. No author build/test/Java run.
