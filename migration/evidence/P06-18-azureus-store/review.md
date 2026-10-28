# Original Azureus STORE packet review

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original
STORE request/reply constructors/serializers, key/value-set array helpers and
request getClockSkew body run through one stream over qualified original
header/value/upload helpers. Only nested value/checked-exception namespaces and
inert supplied packet/getter bridges adapt. Original/adapted source hashes and
frozen helper/core classes are recorded. ReferenceFence is unchanged; no ordinary
manager/core/transport/config/plugin/profile/DNS/network/DB/authority is started.

Requests preserve random ID at7, independent key/value-set counts, byte key
counts/lengths<=255 and byte outer value-set count<=255. Inner value counts are
unsigned16-bit; the source MAX_VALUES_PER_KEY255 constant is unused by these
helpers.300 zero-payload values are accepted within8192. No key20-byte or matching
array-count restriction is invented by the wire codec. Original receive skew
adjusts value creation times with wrapping64 math; outgoing adjustment is0.
The source getClockSkew getter reads the actual field populated by the original
header constructor. Header skew/cursor/cache effects survive later failures.

Replies preserve diversification at6, absent/null before6 versus empty, source
padded reads and network4/version55 optional cached uploads when bytes remain.
Missing interim uploads and malformed optional blocks retain source behavior.
Header/action/network/minimum/version and global failure cursor order match.
Supplied random IDs remain inert wire values, not actual storage authorization.
Host validation, sender partitioning, nonce lifecycle, DB writes/expiry and
routing remain outside this bounded component; no integration baseline advances.

Packets own raw bytes/keys/nested values/IDs and compute trailing views from the
owner. Copy/move/self-move and cleared moved-from offsets are tested. Nested sets
use decodeValuesPrefix, which retains owned decoded fields/consumption and omits
unused remainder copies. Existing single/array value APIs still return owned
tails. Endpoints remain data; no registration or recursive sends occur. Whole
inputs/outputs are capped at8192, while normal1400 datagram budgeting belongs to
transport. Failed partial encodings are diagnostic only and never sendable.

Fresh GCC16, Clang24 and ASan+UBSan+LSan pass all20 combined CTest rows. New
fixtures contain5947 original observations; each compiler additionally passes
1042 source serializer comparisons (512 fresh vectors),2084 Java readbacks
(1766 accepted/318 matching source failures),1416 value serializer comparisons/
2832 Java readbacks,849 contact readbacks/1024 fresh identities and1251 full
FIND_NODE comparisons/2502 readbacks. Native checks cover nested ownership,
binary/empty/mismatched keys/sets, source skew wrapping, null/oversized prefixes,
300 values, prefix-mode semantics, exact8192 requests, action/network/cursor/
skew ordering, actual crypto/thread failure and old/padded diversification.

All original primitive/node/value/PING/FIND_NODE regressions remain passing.
Qt rows have17 and13 passes, zero skips. Each compiler also passes40 actual
FIND_NODE and40 PING source-Java/native exchanges over160 IPv4 datagrams.
Those use only new allowlisted loopback ports and connected fenced Java peers;
no actual STORE service or Java IPv6 socket interoperability is claimed. Native
IPv6 coverage remains separate. No native fixture mismatch or scaffold failure
occurred in this slice's development run. Development logs and source smoke
checks are retained, including exact10ms creation-time adjustment.

Three qualified lower inputs change: value header/implementation/README for the
prefix-only helper. Before/after hashes identify them; all unrelated lower inputs
remain byte-identical. Compiled/tool/fixture hashes, nested original helpers,
OpenSSL3.5.5/Qt6.12 runtimes, commands/logs and binaries are frozen in matrix.json.
Leak detection stays enabled without suppressions. No independent reviewer was
used. Original Java HEAD/index/running client and live profile remain untouched.
Immutable native implementation hashes follow in a later provenance commit.

Continue FIND_VALUE/continuation handling, real anti-spoof authority, lookup,
routing/storage/persistence and dependent plugin/host activation. No operational
DHT, complete plugin/client release or whole-client performance claim is made.
