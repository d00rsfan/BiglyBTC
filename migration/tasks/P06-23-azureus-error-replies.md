# P06-23 Original Azureus error replies

Status: implemented bounded codec; fresh GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P06-23-azureus-error/{matrix.json,review.md}.
Pin before implementation to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f DHTUDPPacketReplyError full
constructor/serializer/getters, action1032, type0/1/2 and original endpoint/length/
byte-array/header helpers. Exact source bodies over immutable qualified header
reference, unchanged ReferenceFence, supplied inert contexts only. No normal
core/profile/plugin/DNS/network/key-block/certificate admission starts.

Preserve signed32 unknown error types with no invented body fields, type1
originator endpoint (null object distinct from unresolved), type2 request byte
length<=255 and signature unsigned16<=65535, source single-read zero padding,
source truncation/cursor/failure order and header versions/networks. No codec
version14 gate invented: host chooses blocked-key errors at>=14 separately.
Own decoded bytes/fields/offsets/tails/copy/move; numeric addresses data-only,
mappedIPv6 receive normalization. Source signature padding may expand a bounded
packet to65535 bytes, preserve it. Native full packet cap8192 and transport1400
stay separate; classify oversized source writers explicitly. Failed partial
output is diagnostic, never sendable. No key-block trust/authority or routing.

Fresh GCC16/Clang24/ASan+UBSan+LSan, source fixtures, fresh serializer and Java
readback checks, bounds/ownership and unchanged header regressions. All lower
qualified inputs stay unchanged; no unnecessary whole UDP matrix repeat for
this standalone codec. Save source/native hashes/evidence/provenance, then
continue owned authorized STORE/error dispatch, routing/storage and plugins.
