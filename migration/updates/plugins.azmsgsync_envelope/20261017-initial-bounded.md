# Original message-sync key/envelope review

Self review, 2026-10-17. Original azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`, bounded init key derivation and
fixSecret/general/private encrypt/decrypt methods. Original SHA1 and vendored
BouncyCastle reference classes come from BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; the full frozen class digest is pinned.

Native preserves folded user-key/SHA1-derived16-byte general key,20-byte listen/
call key and peek bit change; private shared-secret fixing pads short values with
trailing zeros and keeps the last24 bytes of long values. AES128/general and
AES192/private use original IV-prefixed CBC and PKCS padding. OpenSSL3.5.5
supplies primitives and RAND_priv_bytes creates fresh native IVs. Invalid padding
cleanses the intermediate plaintext and returns an error. Original SunJCE general
IV-only input returns empty data, while private BouncyCastle rejects it; native
matches both. This envelope layer does not authenticate a message or authorize
its admission. Original identity/signature/STS verification remains required.

The fenced reference compiles exact extracted method bodies and key block with
supplied fields, pure original SHA1/BouncyCastle and controlled random generation.
No real MsgSyncHandler/plugin/core/DHT startup occurs. The first harness used
ByteFormatter.encodeString diagnostic formatting, which truncates beyond1024
bytes; conversion failed before fixtures were admitted. encodeStringFully fixes
only observation transport. Rejected attempt is retained in evidence.

GCC16, Clang24 and Clang24 ASan/UBSan/LSan pass463 original observations, including
136 original encrypted payloads, malformed input and derivation cases.136 fresh
native encryptions per compiler reopen identically in original Java, with136
distinct IVs. Bounds/freshness tests also pass. Matrix records sources, method/
class hashes, commands, actual binaries and OpenSSL headers/runtime. Leak detection
remains enabled; no whole application/protocol/security claim is made.

Native bounds payloads4MiB and user-key/shared-secret inputs64KiB; mode keys are
exactly16/24 bytes. Larger/unexpected provider-key inputs are not qualified. Full
signature/identity/STS, persisted history/identity ownership, wire maps/compression,
replay/order, DHT/database transfer, public/I2P separation and Qt plugin lifecycle
remain required. No real profile, network, chat or user identity was accessed.
GPL2-or-later source and original Azureus copyright/full notice are retained.
