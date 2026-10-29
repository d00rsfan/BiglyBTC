# Bounded original I2P client identity prerequisite — 2026-10-29

`services.i2p_client_identity` / `F06.i2p_client_identity` remains **in_progress**.
Initial source assessment, with no previous integrated baseline: official I2P
`9134f808337b401e8e53c73734c81fab04280c9d` and plugin
`33d97eca67a9f332e3d4da7e7492ec54f4bec078`. Both first-port and integration hashes
remain null. Native implementation commit: **5dfbd587e16e86122edd548f861a67430c20586d** (root fills the actual
adoption hash before applying this proposal). Existing `tor-openssl-host`,
`i2p-java-reference-2-13-0`, and `azi2pcore-reference-2-13-0-1` inputs are reused.

The source implements online EncType0/signing0 or7 key streams: original public
Destination,256 opaque unused encryption-private bytes,20/32 signing-private
bytes; exact663/667/679 widths,1024 input cap, move-only ownership and cleansing.
It validates the signing key against the public key and signs default Datagram2
using the already qualified codec. All-zero signing key means unsupported offline
format; nondefault encryption, unsupported forms and trailing data refuse. Earlier
signpair refusal than the original constructor is deliberate safe prevalidation;
unused encryption bytes do not prove an ElGamal pair.

The source-only capsule1502245f6e23427be40142128906725f88c62589513bc24ecc82fb9557307bfb
and independent source review08df6dc4ee57b5169f550e2e46e357b6b4980c73f6b31717c9c7c2b265cbdd32
cover ownership, bounds, source order and authored fixtures. `pure-i2p-client-identity`
(30s) covers both algorithms, all truncations, caps/trailer/offline/key mismatches,
opaque bytes, explicit DSA certificate, move/clear and imported signing. The
root-only offline bridge is authored for exact official JARd76b3c4c4fa7043c2a3553ab3209954e63151dfd1a90f7c020fbc58b33c89896
and modified JARb0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5:
four actual unconnected reader/export/signing cases each, including negative
recipient/payload/signature verification. **Compilation, ordinary tests and this
identity interop have not yet run.** The previous datagram-only interop is a
separate historical result and grants no new identity execution credit.

Pending acceptance belongs at
`migration/evidence/20261029-direct-i2p-identity/verification.json`; that evidence
path is reserved, not a claimed PASS or existing receipt. Root must publish actual
source/image/command/results before applying the registry reference. Reviewer:
error_oracle; independent source reviewer: error_codec. Current GCC/Clang/SAN and actual offline four-case-per-reference acceptance passed; see migration/evidence/20261029-direct-i2p-identity/verification.json.

No profile discovery/writer, automatic key generation, offline/transient identity,
I2CP framing/CreateSession, connected session, transport, router/backend activation,
SAM equivalence or whole modified-core/source equivalence is included. Full P07
and original source integration remain open. No upstream A-to-B review is implied.

Qualification addendum: actual native implementation 5dfbd587e16e86122edd548f861a67430c20586d; current qualified source 5abc17533521c881971d4e3f5551ff2963d4c648 differs only in the HTTP CTest timeout. Identity native tests originate at the implementation; the exact image is unchanged. Offline-wrapper-r2 passed four actual cases per official/modified reference using that implementation ELF and corrected concrete factory bf062; failed Java r1 remains retained. Earlier pending statements above describe the initial source-only review, not the current acceptance. See migration/evidence/20261029-direct-i2p-identity/verification.json. Component and both source integration baselines remain in_progress/null.
