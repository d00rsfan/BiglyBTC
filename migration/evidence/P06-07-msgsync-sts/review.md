# Original message-sync STS compatibility review

Core baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azmsgsync application
baseline4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. The reference instantiates the
unchanged pure CryptoSTSEngineImpl through its package-private constructor, with
known public test identity scalars1,2 and2^191, fresh original ephemeral keys and
the frozen vendored BC provider. It never invokes a manager/plugin constructor,
CoreFactory, actual profile/identity file, DHT service or network. The already
qualified original fixSecret/private CBC bodies consume the actual derived keys.
ReferenceFence excludes real user profiles and unowned network/process/JNI access.

Native prime192v2/SHA1/ECDSA/ECDH use OpenSSL3.5.5 EVP, not custom curve math.
Imported identity pairs/public points are checked; fresh native ephemeral keys
and cryptographic nonces are generated. Raw point compression is preserved for
25-/49-byte identities; the original vendored getEncoded(false) follows the
point's stored compression flag, unlike an ordinary assumed uncompressed export.
The native output compression is set explicitly through the documented
[EC point-format parameter](https://docs.openssl.org/3.5/man7/EVP_PKEY-EC/).
Peer validation uses
[EVP derivation validation](https://docs.openssl.org/3.5/man3/EVP_PKEY_derive/).

The shared secret preserves vendored DH's positive BigInteger.toByteArray form,
not fixed24-byte ECDH output. Leading zero bytes are stripped and the Java sign
byte restored where needed. Both the rare23-byte path (original fixSecret copies
then pads on the right) and ordinary24-/25-byte paths pass actual mutual auth and
original private CBC readback. Incoming field framing, original cursor advancement,
phase checks, trailing-byte acceptance and partial candidate-public-key state
match the stated fixture domain. Key packets alone do not authenticate a session.

The application wrapper exposes derived-state diagnostics but grants secret
access only after valid remote auth and exact expected encoded identity binding,
as MsgSyncHandler's normal application path requires. It does not expose the
original raw pre-auth secret getter. Failed auth retires that access until a
valid proof; moved-from/reset owners cannot grant access. Issued owning secret
copies remain the caller's responsibility. Internal raw derivation, private
parameter scratch and owning secret buffers are cleansed; tests observe clearing
on moves without reading freed memory. No guarantee about all external/provider/
caller copies is asserted. Host owner-thread discipline remains required.

313 original cases yield1033 cursor/state observations plus225 integer vectors.
Two explicitly listed rows differ: the original accepts a signed off-curve(0,0)
ephemeral point; native validation rejects it and derives no secret. Original
goldens remain unchanged, and native-policy.json pins the separate required
rejection by command hash. See invalid-point-disposition.md. Native packets are
bounded at128KiB before mutation; arbitrary malformed/oversized original input
acceptance is not part of the qualified domain. No complete source integration
baseline or release gate is advanced around these input/interface differences.

GCC16, Clang24 and ASan/UBSan/LSan pass all four CTest rows,65 original-Java/native
mutual handshakes and780 private ciphertext readbacks per compiler. Identity
roles cover67 compressed and63 uncompressed encodings;32 equivalent ephemeral
encodings are re-signed through original identity signing. Expected-key mismatch,
pre-auth access, bad proof/retry, input limits, reset/move and secret ownership
are checked. The existing463 envelope fixtures and136 fresh Java readbacks pass.
The earlier successful narrower matrix is retained; expanded encoding coverage
supersedes it. All compiler/source/class/fixture/binary hashes are recorded.
Leak detection remains enabled. No independent reviewer was used.

Actual persisted identities/passwords/history, complete message signatures,
bencoded/compressed DHT request maps, replay/order/timeout handling, real DHT and
private-session host activation, lifecycle/UI and full compatibility/performance
qualification remain required. A successful primitive is not an operational chat
plugin or a binary release. Public source export still needs independent rebuild,
exact committed-blob checks and immutable publication provenance.
