# Initial partial I2P datagram review — 2026-10-29

Component `services.i2p_datagram`, F06.i2p_datagram, remains **in_progress**.
This is initial baseline assessment, not an upstream A-to-B update: official I2P
`9134f808337b401e8e53c73734c81fab04280c9d` (tree `b38c70fbf3f7ce6f33ca95bedaa001e1458a4c9b`) and
azneti2phelper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. No prior integrated baseline exists; first_ported and
integrated_through remain null. Last-reviewed identifies these inspected sources.
Native implementation is `ecd63457da93e7f0c993c1f684e6d50d1c79a48c` in BiglyBTC. Review author: error_oracle;
independent Codec source review and root current-cohort acceptance are complete.

Actual credited evidence is limited to offline default Datagram2/3: the official
Maven JAR/source/Git relation and exact modified ZIP/JAR are pinned separately.
Each reference produced8 original signed2/unsigned3 rows and verified8 native2/3
rows (DSA0/Ed7, empty/binary/1024/max); wrong recipient/signature/nonempty payload
negatives pass both directions. Modified class-load logs show the actual retained
JAR, not the official classpath. Existing fixture class/native ELF were reused,
with unchanged input/library hashes and exact current two-file production source.
8 unsigned3/envelope pairs and4 deterministic Ed signatures match across references;
randomized DSA signature equality is not required. Official initial compiler failure
and all original successful receipts remain immutable. The ordinary fixture CTest passed in actual current GCC/Clang/SAN runs; separate current52 suite/private results are linked in migration/evidence/20261029-proxy-i2p/verification.json, not inferred from offline reference runs.

| Relevant source/dependency behavior | Disposition |
| --- | --- |
| Online/default0/7 signed Datagram2; recipient binding; common Destination | Implemented and offline cross-reference observed; current-cohort acceptance passed for this bounded subset |
| Datagram3 claimed sender/payload envelope | Implemented unsigned; explicitly no authentication/admission authority |
| Extra flags/offline/options/other signing and certificate forms; Datagram1 | Explicitly unsupported; not counted integrated |
| Plugin default maker calls and DSA0/Ed7 defaults | Source-backed bounded call shape; no session/router activation |
| Modified core1654classes/549 shared class binaries differ | Exact artifact resolved; whole source/core/JNI/streaming equivalence remains open |
| Vetted native crypto and notices | Reuse exact `tor-openssl-host` SHA; reference-only Java inputs separately pinned; full modified-bundle shipping notice closure pending |
| Router/client/backend/streams/profile/public I2P/performance | Not implemented or qualified by this component; broader P07 and AD-005 work remains open |

The official tag/commit/full tree and21 needed source blobs are durably retained
in isolated shallow bare `build/dependencies/i2p-java-2.13.0`; exact Git reads were
checked with lazy fetching disabled. This is not a full-history/source-fork claim.
Source/cryptographic implementation remains pure non-Qt and has no process/socket/
profile API. No real profile, router or public endpoint was started. Current native/sanitizer and separate private HTTP/tooltip results are recorded explicitly; no shipping/whole-plugin parity is claimed here.
