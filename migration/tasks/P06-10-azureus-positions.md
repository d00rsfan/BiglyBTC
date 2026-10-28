# P06-10 Original Azureus reply position wire fields

Status: implemented bounded wire component, qualified locally. Whole transport/
release integration pending. Pin cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Continue
the PING reply path with original DHTUDPUtils Vivaldi framing, manager provider
selection/reset and unchanged pure Vivaldi v1 provider/factory wire behavior.
No coordinate estimation/update/randomization algorithm is in scope yet.

Use exact original framing and manager deserializer method bodies with explicit
supplied provider registration and no listeners, plus actual frozen source-built
VivaldiPositionProvider/Factory/Impl. ReferenceFence prevents user profile and
unowned network/process access. Never initialize ordinary DHT/core/config/plugin
managers or alter global registrations in a running application.

Characterize old versions10-14, generic version15+, optional Vivaldi51+, fallback
NaN point, empty/null providers, count byte narrowing, signed size skips, unknown
providers, known provider reading16 bytes irrespective of declared size, failed
provider read/reset followed by skip, float NaN canonicalization and tail offsets.
Known positions own canonical wire values; unknown outgoing provider payloads are
supplied immutable bytes. Future host provider/listener lifecycle remains work.

Bound input/output at PRUDP8192 bytes, check ownership and partial failures, run
original fixtures/readbacks, fresh GCC16/Clang24/ASan+UBSan+LSan and record exact
immutable native/source/classes/fixture/binary hashes. No complete PING reply,
UDP transaction actor, routing/storage, plugin or runtime activation is claimed.

Result:8554 original observations, two CTest rows,2494 original encoder
comparisons and4988 accepted/failure Java readbacks per compiler pass on fresh
GCC16, Clang24 and ASan+UBSan+LSan. See
migration/evidence/P06-10-azureus-positions/{matrix.json,review.md}. Continue
alternative-contact framing/age/identity and full owned-loopback PING dispatch.
