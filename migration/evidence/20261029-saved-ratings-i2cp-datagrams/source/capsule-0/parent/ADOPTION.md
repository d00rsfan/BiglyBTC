Source-only extension of services.i2cp_initial_session. Six leaves: existing
channel.hpp/cpp, new pure i2cp_datagram/messages.hpp/cpp and two focused test
files. Append services/tests CMake.fragment sections once. Exact source basis
6726a4cf7b017ac31d6aeede15f339c3ec2b71f7; metadata descendants require exact
beforeimage checks. No author compile/Java/test/endpoints/N/profile/shared
tracking writes. Existing borrowed registration and owned LS2 APIs stay intact.

Explicit outbound-only submitDatagram2 / submitClaimedDatagram3 requires the
current retained signer, exact accepted full local Destination/session, actual
successful LS2 enqueue and a still-future LS2 latest expiry, using router date
plus monotonic elapsed time. CREATED alone never grants submission. The full
explicit recipient Destination is decoded anew; Datagram2 signs for its exact
hash. Datagram3 only claims sender hash and is named accordingly. No resolver,
listener/Streaming/Host/backend/public/default activation or encryption transport
is added. It is a usable local external-router submission/control prerequisite,
not a claim of tunnel health, router acceptance or end-to-end delivery.

One pending status-requesting nonce, one socket write and one latest observation
are bounded. Nonzero uint32 nonces never repeat during this QObject's lifetime,
including explicit restarts; exhaustion refuses. There is no message-ID-only or
nonce0 fallback. The initial reliability setting is immutable, matching original
ClientConnectionRunner.destinationEstablished: case-insensitive exact none
refuses tracked sends even after reconfigure to BestEffort; initial status-enabled
sessions stay so after reconfigure to none. Existing native APIs deliberately do
not inject original saved profile defaults. This explicit tracked API does not
silently override none or implement original fire-and-forget nonce0 policy.

Normal original Accepted and later status preserve both client nonce and router
message ID. Error paths may emit a terminal without Accepted, so the first
current-nonce status binds its message ID; later current-nonce IDs must agree.
Strictly retired nonces on the same current session are ignored. Wrong session,
future nonce, changed current message ID, unsupported/incoming status or malformed
frame withdraws the channel. No late status can finish a later nonce. Original
router suppresses repeated non-accepted status per message ID; this finite API
treats the first known non-Accepted status as its terminal observation, preserving
raw code/certainty. Accepted1, unused legacy success2, probable success4, local
success6, probable failures3/5 and failures7..23 remain separate. None is relabeled
as application acknowledgement or guaranteed end-to-end delivery.

A separate stable timer owns an immutable local status deadline, including
preparation, within the existing caller-selected1..30000ms budget. Renewal can
update only its lease fence, never extend/cancel that deadline. Reconfigure is
refused while a datagram is pending; dispatch is refused during reconfiguration.
Local cancel/timeout/lease-expiry emits a local phase with NO fabricated router
code. It does not remotely cancel queued traffic and never retries/resends.
Close/transport failure clears all observations and owned session keys. Public
notifications are bounded/coalesced queued CURRENT observations, permit reentry
or deletion, and do not promise a queue of every intermediate state. A failed
enqueue/notification may leave transmission uncertain; callers must not infer a
safe retry from failure. Last completed observations are historical, not rights.

Pure DTOs implement Send5/Expires36, Status22 and inbound Payload31. Expires uses
original flags16 then date48, flags0 only. Session ID65535, malformed/trailing
lengths, unsupported flags/statuses and Payload>65536 refuse. Message expiration
must be future at dispatch; it remains router TTL, separate from local status
waiting. Payload31 has NO channel listener/receive admission here; original
fast-receive/ReceiveBegin/End handling remains separate. No receive rights are
granted by parsing it.

The mux envelope is real bounded gzip with CRC+ISIZE checked by existing exact
zlib1.3.1 (already required by adapters), original protocol19/20 in byte9 and
big-endian from/to ports in bytes4..7. XFL2 matches the original fixed header.
Explicit stored or deflate level3 uses the existing zlib API; original complete
uncompressed datagram limit is40KiB, not the datagram maker's61KiB. Output is
bounded64KiB. One fixed-header gzip member only; optional fields, concatenation,
trailing data and expansion above40KiB refuse. This is a declared finite subset
of general gzip parsing, with no guessed compression/default setting mapping.

Targets/CTest: biglybtc-i2cp-datagram-tests / pure-i2cp-datagram-messages (60s),
biglybtc-i2cp-datagram-channel-tests / i2cp-lease-backed-datagram-channel (60s).
Meaningful retained checks include existing channel, signed session, LS2 and
datagram/identity units because the channel uses those actual capabilities.
New tests cover actual owned v4/v6 signed/claimed wire, recipient hash, gzip,
Accepted/final distinction, terminal-before-Accepted, stale/wrong nonce/id/session,
initial immutable none, local cancel/timeout, renewal independent deadline, actual
lease expiry/renewal, malformed/bomb bounds and deletion from public callback.
No old PASS is claimed as fresh qualification.

OFFLINE-COMMANDS.sh compiles one Java object helper and runs each pinned official
and modified JAR separately with an already-built current native pure test ELF.
Eight signer0/7 × Datagram2/3 × stored/deflate cases cross both directions using
actual SendMessage/Expires, DataHelper gzip and Datagram2/3 objects; four actual
Status22 and one Payload31 field vectors roundtrip byte-exact. Random signatures
and deflate bitstreams are cross-verified/decoded, not compared for equality.
Root checks exact eight row sets/direction/reference, final status/payload marker,
exact file:/JAR origins and source/ELF before-after under one180s budget. No
session.connect/router/security fence; JDK crypto loads before any external
fence. Synthetic test keys only. Modified whole-core/source equivalence remains
open. Source pins separately record source-JAR equality and actual retained Git
blob availability; only exact tree-matching missing blobs are materialized.
