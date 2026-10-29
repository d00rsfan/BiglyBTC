Source only, based on native8849b3cc2d2c13771fde3381d66beadf173fbe48.
Apply the five exact source maps/changes.patch; no CMake changes/new targets.
No native/plugin/shared registry, compiler, router or runtime subject was changed
or executed by the author. The approved proposal remains immutable separately.

Generic absent `i2cp.leaseSetEncType` stays Enc0. Explicit `4` or ASCII
case-insensitive `ECIES_X25519` selects one Enc4 key. Mixed/multiple/unknown types,
configured private-key restoration and encrypted/meta/offline LS2 remain refused.
This implements the useful explicit client capability. Future plugin projection
must supply its original default4; no Host/backend/default activation is added.

The independent key owns maximum256-byte private/public arrays; typed views and
wire fields expose32 bytes for4. OpenSSL's existing pinned Crypto derives the
X25519 public key from original raw scalar bytes. Imported raw clamp bits remain
unchanged; fresh generation follows original byte31-nonzero selection, with an
explicit eight-candidate failure cap. Moves, clear and private-frame cleanup
erase the full owned arrays. No handwritten encryption/curve/ratchet is added.
Destination Enc0 identity, padding, signing0/7, key files and hashes are unchanged.

The original LS2 type3 signature/time/options/lease rules remain. Enc4 emits
one public type4/length32 and one matching private type4/length32 in Create41.
The decoder parses LS2 to its consumed offset before parsing the private tail;
no guessed backwards boundary. Original Enc0 output still uses256-byte keys.
Current merged policy seals the initial key type before any reconfigure write;
ordinary updates omitting the type keep4. Renewals reuse the same real keypair.
Secret-frame/LS2/lease limits remain4096/2048/16. No lease enqueue means router
acknowledgement, readiness, application delivery or anonymous tunnel authority.

Default arguments keep C++ source calls valid, but the generate/fromPrivateKey
symbols gain a parameter and two internal DTOs gain a type field. Rebuild their
native dependents together. These are internal static-library APIs; no public LT
SDK/provider ABI changes or provider/Qt rebuild are required.

Meaningful affected ordinary controls retain their existing target names:

- `biglybtc-i2cp-leaseset-tests` / `pure-i2cp-leaseset2`: existing Enc0 rules plus
  Enc4 DSA0/Ed7,1/16 leases, clamp equivalence/raw preservation, generation,
  actual move/clear/frame erasure, malformed type/width/private pair/trailer,
  unchanged identity and key-stable renewal. The obsolete4 Unsupported row now
  uses5, with separate positive4/alias assertions and mixed-type refusals.
- `biglybtc-i2cp-initial-channel-tests` / `i2cp-initial-channel`: four old renewal
  rows retain absent Enc0 and add two explicit Enc4 rows. Fragmented real37→41,
  exact current Destination/session, owned reconfigure, type-change no-effects
  refusal, genuine UPDATED/renewal and disconnect withdrawal remain asserted.
  Its obsolete4 Unsupported input also becomes5. Other method bodies stay exact.
- Existing lease-backed datagram/receiver/tracker and Streaming ordinary controls
  remain useful integration regressions because all share the channel/key API.
  No old Java/reference/provider replay is requested as evidence for those calls.

`OFFLINE-COMMANDS.sh` is a finite root-only dual-JAR object recipe using the same
current LS2 test executable with `--enc4-java-interop`. It compiles one Java8
helper once against the pinned official JAR, reuses it with officiald76b and
modifiedb086, and requires exact class origins. Each reference supplies four
actual Enc4 Create41/request pairs (DSA0/Ed7 ×1/16; raw clamp variants). Native
verifies signatures/pairs and byte-exact original reserialization, produces four
fresh frames, and Java verifies signature/pair/fields/exact reserialization plus
changed-signature refusal. Random signatures from different producers need not
match. Root supplies a JDK compatible with both retained artifacts and records
actual commands/ELF/class hashes/logs; no router/session/connect occurs here.

`isolated-router/` provides a separate source-only actual Enc4 acceptance recipe.
It derives from the proven immutable Enc0 foundation, keeps the same hard fence,
and requires both actual accepted LS2s to have one type4/32-byte key. Exact
warmed-local tracker connect/scrape/RAW18 counts and actual retirements remain.
This proves I2CP key registration/local exchange only. It cannot prove remote
ECIES-ratchet, public/multihop tunnels or full plugin/backend compatibility.
