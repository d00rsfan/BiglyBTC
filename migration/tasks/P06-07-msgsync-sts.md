# P06-07 Original message-sync identity and STS handshake

Status: bounded primitive qualified and source export published.
Native message envelopes need the original authenticated
private-session key agreement. Characterize the unchanged original core
CryptoSTSEngineImpl, CryptoECCUtils and vendored JCEECDHKeyAgreement at
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; application usage is MsgSyncHandler at
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. The earlier legacy ECDSA probe is only
background evidence, not a complete native identity/handshake implementation.

Allowed write paths: native/plugins/BiglyBTC-plugin-azmsgsync identity/STS files,
test/build consumers, task-local source oracle/fixtures/tools/evidence and shared
tracking. Original Java and plugin repositories remain read-only. Use OpenSSL
3.5.5 EVP operations, C++23 and the pinned GCC16/Clang24/libstdc++16 toolchains.
No custom curve arithmetic or new crypto policy. Preserve original prime192v2,
SHA1/ECDSA, raw key formats, version1 length-prefixed messages, signed key packets,
auth proofs and the vendored BigInteger.toByteArray shared-secret representation.
The latter is not necessarily24 bytes; the already-qualified message-sync
fixSecret behavior must consume the same original bytes.

Reference helpers may instantiate the pure original STS class with generated
identities/ephemeral keys and the frozen vendored BC provider, under ReferenceFence.
No normal CryptoManager/plugin/CoreFactory startup, profile/keystore access,
actual DHT service, Tor, DNS, listener or public traffic. Generated-only Java and
native helpers can exchange messages over owned stdin/stdout or owned temp files.
No actual user identity/key material is read or published. Any retained synthetic
test-key vectors must be unmistakably test-only and unusable as shipped identities.

Characterize phase errors, malformed/truncated/framed messages, raw key/signature
acceptance, state after failures and variable-length secrets before implementing
the bounded native primitive. Verify original-Java/native exchanges in both
directions with fresh native ephemeral keys/nonces and encrypted payloads after
authentication. Preserve secret ownership/cleansing and expose verified peer state
explicitly. The source primitive offers some raw getters before auth; application
use must still enforce successful peer auth and exact expected-peer key binding,
as MsgSyncHandler does. Do not mistake a self-signed key packet for trusted identity.

Run GCC, Clang and ASan/UBSan/LSan with leak detection enabled, serial nice5/two-job
builds. Keep existing envelope fixtures and native-to-Java readbacks. Full identity
file persistence/password handling, DHT request maps/timeouts, replay/identity
binding at the host, private-session activation, routing and Qt UI remain pending.
Record exact immutable source/native/dependency hashes and all scope differences.

313 original input/phase cases produce1033 observations;225 original positive
integer byte vectors pin all widths/sign-bit cases. The unchanged original also
accepts a signed off-curve (0,0) ephemeral point. Native rejects it through OpenSSL
validation; the exact two state differences are declared separately, with command
hashes and untouched source observations. No full malformed-input equivalence is
claimed and no integrated/release baseline advances around that difference.

All four CTest rows pass GCC16, Clang24 and ASan/UBSan/LSan. Per compiler,65
complete original-Java/native mutual handshakes and780 private-cipher readbacks
pass. They include67 compressed25-byte and63 uncompressed49-byte identity roles,
32 equivalent uncompressed ephemeral packets re-signed by the actual original
identity signer, and actual23-/24-/25-byte shared secrets. A bounded Java-only
candidate search obtains the rare23-byte case before completing native mutual
auth and encryption. Original expected encoded peer binding is tested explicitly,
including equivalent points with different encodings. Earlier narrow successful
coverage is retained before-uncompressed-coverage; the expanded matrix supersedes
it. The existing envelope463-case fixtures and136 fresh native-to-Java readbacks
also pass per compiler. Native ownership/reset/phase/limit tests retain caller
inputs and verify moved secret buffers are cleared. No leaks are suppressed.

Local implementation:3d823b31c24285d1e586f655c0bbe24f9b1d6408.
Public source:3d8e761639b59b4310d1950b7227cfae4272d749. All29 committed files
match provenance, and a fresh public source rebuild passes all four CTest rows.
No operational plugin, DHT or binary release is enabled. Continue shared original
Azureus DHT transport work required by ratings, related content and message-sync.
