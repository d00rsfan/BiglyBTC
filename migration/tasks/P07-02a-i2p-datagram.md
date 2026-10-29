# P07-02a bounded I2P Datagram2/3 prerequisite

State: in_progress. Feature F06.i2p_datagram; P01 wire/identity feasibility and
P07-02 adapter prerequisite. Native implementation `ecd63457da93e7f0c993c1f684e6d50d1c79a48c` exists; current
52-file GCC/Clang/SAN, runtime/private and retained-vector CTest acceptance
passed; see migration/evidence/20261029-proxy-i2p/verification.json. First-ported/integrated hashes stay null. This closes no router,
client, stream, session, profile, transport or public-network family.

Port only online/no-options Datagram2 for DSA_SHA1(0) and Ed25519(7), common
Destination/key-certificate envelope, intended-recipient binding, and unsigned
Datagram3. Use vetted OpenSSL EVP through existing `tor-openssl-host`; exact
runtime SHA matches that lock. No new primitive or anonymity route/backend.
Datagram3 sender is merely claimed; Datagram2 does not authenticate serialized
sender encryption-key/padding bytes. Unsupported flags/algorithms/certificates
refuse explicitly. Synthetic fixture keys are public test-only values.

Official upstream `9134f808337b401e8e53c73734c81fab04280c9d` and plugin `33d97eca67a9f332e3d4da7e7492ec54f4bec078` are separate baselines.
The exact modified distribution embeds azi2pcore_2.13.0.1.jar; its separate source
commit is unknown. All41 distributed wrapper Java sources match pinned plugin;
that is not whole-core source equivalence. Notices and immutable artifact pins
are in [reference inputs](../evidence/20261029-i2p-datagram/reference-inputs.json).

Actual offline acceptance already passed for BOTH official and modified Java
references: types0/7 x empty/binary/1024/max,8 rows each direction for Datagram2
verify and Datagram3 decode, wrong-recipient/signature/payload rejection. Official
first strict GCC warning failure is retained; successful run uses corrected exact
buffer bounds. Modified run compiled nothing and reused pinned images whose two
production codec files match current implementation. Ordinary CTest reads retained
original-produced binary vectors without Java/download/Qt; its current-cohort
execution passed on all three compiler lanes. All raw command/class-load/input/vector receipts stay
in their frozen build capsules; no old PASS is relabeled as a new full run.

Root reviewed the actual current six/three/six suite results and three private HTTPS/tooltip rows, with immutable evidence above. Both source integration baselines remain null because broader source behavior is incomplete. Continue offline/options/other algorithms and stream/router/client prerequisites separately; whole P07 remains open.
