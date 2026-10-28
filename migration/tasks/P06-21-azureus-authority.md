# P06-21 Original anti-spoof authority

Status: implemented bounded primitive; all fresh compiler/sanitizer checks pass.
Evidence: migration/evidence/P06-21-azureus-authority/{matrix.json,review.md}.
Source pinned before workspace implementation to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f DHTControlImpl.generateSpoofID,
generateSpoofID2,verifyContact, constructor storage-supported digest/key setup,
and two256-entry access-order LinkedHashMaps. DHTTransportContact type1 and actual
HashWrapper are included. Implementation remains a standalone owner-thread
primitive; no full source integration baseline advances.

Use per-instance CSPRNG16-byte epoch key, no persistence/rotation/global singleton.
Preserve MD5/XOR/truncation/signed int and separate IP/fullID cache semantics,
source port/scope independence, mapped-factory differences, disabled zero tokens,
verification null/length order. Own returned values, cleanse key/scratch. Bound
nativeID memory8192, actualUDP20. Explicit provider/random errors fail closed
instead of source caught-exception zero fallback; record original policy rows
and do not silently mark full compatibility. No normal Control ctor/core/config/
profile/DNS/network/plugin/STORE/DB/routes or real identities are accessed.

Characterize exact source bodies under unchanged ReferenceFence with supplied
inert monitor/logger/contacts and public generated keys, actualMD5/HashWrapper.
Test all address-byte alphabets, key extremes, IPv4/IPv6/port/scope/mapped behavior,
IDs0..64, prefix/fullID cache distinction,256/257 eviction/access refresh,
independent caches, verification correct/wrong/null/truncated tokens, disabled
state, owned return mutation, real provider failure and owner thread/lifecycle.
FreshGCC16/Clang24/ASan+UBSan+LSan; new native tokens must be independently
verified through actual original verifyContact. All lower owned-UDP input hashes
stay unchanged; do not needlessly repeat that full historic matrix.

On success freeze evidence/native hashes with separate provenance checkpoint,
then continue authorized STORE/lookup/routing/storage/persistent host/plugin
integration without asking or stopping. Keep original Java HEAD/index/client and
live profile untouched. Questions stay in migration/QUESTIONS.md.
