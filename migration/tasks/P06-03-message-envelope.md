# P06-03 Original message-sync keys and encrypted envelopes

Status: bounded envelope/key implementation qualified across GCC16/Clang24/
ASan+UBSan+LSan; message authentication/protocol integration pending. Original azmsgsync baseline
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`, bounded key-derivation block and
fixSecret/generalMessageEncrypt/generalMessageDecrypt/privateMessageEncrypt/
privateMessageDecrypt. Do not initialize the real handler/plugin or DHT.

Characterize the exact extracted methods with frozen original BiglyBT SHA1 and
vendored Bouncy Castle helpers, supplied keys and generated data. General messages
use the original user-key-derived16-byte AES key and IV-prefixed CBC/PKCS5
framing; private messages use24-byte shared-secret fixing and the original CBC
padding. Native uses OpenSSL3.5.5, cryptographically random IVs, bounded buffers,
explicit errors and secret cleanup. Check original -> native and native ->
original payloads, including empty/block boundaries and malformed envelopes.

This is the original envelope layer, not message authentication. Existing
signature/identity/STS verification, protocol maps/compression, replay/order,
DHT transfer, persisted chat/identity lifecycle, network separation and UI remain
required. No chat, public DHT, saved user identity or real profile may be used.

463 original observations include136 generated original encrypted payloads,
channel-key/shared-secret derivation, malformed/truncated/padding cases and the
SunJCE general IV-only empty-result versus private BC rejection distinction.
Native production encryption uses RAND_priv_bytes;136 fresh native envelopes
per compiler reopen identically in the frozen original Java methods. Limits and
IV freshness checks also pass. Initial ByteFormatter diagnostic truncation was
rejected before fixture generation and corrected with encodeStringFully only
in the harness observation transport. No original method was changed.
