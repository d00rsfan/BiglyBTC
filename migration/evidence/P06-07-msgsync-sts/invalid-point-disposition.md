# Original invalid-point observation

The unchanged core at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
accepts a correctly identity-signed key frame whose ephemeral field is SEC1
uncompressed (0,0). This point is not on prime192v2 (its curve coefficient b
is nonzero). The original derives a secret instead of rejecting the point.
The source observations remain unchanged in msgsync-sts/expected.txt, rows
677/678 (zero based), and the corpus manifest pins their exact bytes.

The native primitive will validate imported public points through OpenSSL EVP
public_check/derive_set_peer and reject this input before deriving any secret.
The exact two affected expected native responses are separately declared in
native-policy.json, with command hashes and original responses. This is an
explicit input-validation difference, not a claimed Java acceptance match or
an altered golden observation. Normal original-generated identity/ephemeral
keys remain the required interoperability domain. Full component integration
hashes/release qualification remain open. No user profile or personal identity
was read, and no real peer or network was used to make this observation.
