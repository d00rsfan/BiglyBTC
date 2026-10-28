# P06-19 Original Azureus FIND_VALUE request/reply packets

Status: implemented bounded wire slice; all three fresh v2 compiler/sanitizer matrices pass.
Evidence: migration/evidence/P06-19-azureus-find-value/{matrix.json,review.md}.
Pin before implementation to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Continue the original DHT wire services
required by rating/message-sync. No normal managers/core/config/plugin/profile,
DNS/public network, DB writes or token authority. ReferenceFence stays unchanged.

Characterize exact one-stream request/reply constructors/serializers and setters.
Requests action1030: target0..64, body flags signedbyte distinct from header flags,
maximum-values setter only caps values>255 and otherwise wraps low8; getter is
unsigned. Null/oversized target and partial required fields preserve prefixes.

Replies action1031: continuation at>=6, is-value boolean always; any nonzero wire
boolean is true. Value branch: diversification at>=6/default DT_NONE1, values
array read skew0, serialization adjusts -remote clock skew with LONG_MIN wrapping.
Contact branch: ordinary contact array/port0 filter first, then Vivaldi at>=16.
Retain null versus empty values/contact mode, provider/reset/cursor ordering and
unknown/signed diversification values. No upload block belongs to these packets.

Reuse qualified header/value/contact/position helpers. Copy/pin original reply
getClockSkew body and supply remote-contact skew through an inert subclass,
without changing qualified original reference helpers. Original/adapted method
hashes record nested type/exception namespace changes and supplied packet bridge.
Bodies/IDs/keys/positions/raw packets/tails own storage; moved owners clear views.
Cap whole packets at8192; transport1400 remains separate. Returned endpoints are
inert. No continuation actor/request lifecycle or storage/routing activation yet.

Qualify original fixtures, fresh Java serializer/readbacks, native bounds/clock/
ownership/cache/provider/thread cases and GCC16/Clang24/ASan+UBSan+LSan with prior
STORE/value/node/PING/FIND_NODE tests and owned Java datagrams. Record immutable
source/native/class/fixture/binary hashes; preserve Java HEAD/index/running client.
Then implement source-qualified continuation delivery (each part plus final or
error), real anti-spoof authority, lookup/routing/storage/persistence/host services.
