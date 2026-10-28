# Original Azureus FIND_VALUE packet review

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original full
request/reply constructor/serializer bodies, maximum-values setter/getter and
reply clock-skew getter run over qualified original header/value/contact/position
helpers and frozen classes. An inert remote-contact subclass supplies its clock
skew to the original base constructor. Nested type/exception namespaces and an
inert packet/getter bridge adapt only reference types/context. Original/adapted
hashes are recorded. No ordinary core/manager/config/plugin/profile/DNS/network/
DB/token authority starts. ReferenceFence remains unchanged and verified.

Requests preserve0..64 targets and padded/partial required fields. Body flags
shadow the generic header flags independently. The maximum setter only caps
values>255, negatives wrap low8, and the getter returns unsigned. Reply modes
retain null values/contact branch versus nonnull empty value arrays. At>=6,
continuation/diversification exist; earlier defaults arefalse/DT_NONE1. Nonzero
wire booleans all read true; signed/unknown diversification remains unchanged.
Value serialization adjusts negative remote skew with LONG_MIN wrapping; value
reads adjust0. Contact arrays/port0 filtering precede Vivaldi at>=16, including
source fallback/provider reset/failure cursor and earlier cache effects. There
is no upload block. No extra sender/host/storage validation is invented here.

Raw packets/targets/keys/values/contacts/IDs/positions and computed trailing views
are owned. Copy/move/self-move and both nested reply-copy assignment transitions
are tested; moved-from offsets/body clear. Value mode uses prefix parsing without
unused-tail copies. Crypto/thread failures stay explicit at global cursors,
without invented IDs. Whole packets are capped at8192; normal1400 transport
budgeting stays separate. Failed partial output is diagnostic, never sendable.
Endpoints remain inert and no request continuation lifetime is activated by this
codec. No full source integration baseline advances.

Fresh v2 GCC16, Clang24 and ASan+UBSan+LSan pass all22 combined CTest rows.
New fixtures contain5294 source observations. Each compiler additionally passes
1175 source serializer comparisons (512 fresh vectors),2350 Java readbacks,
1042 STORE comparisons/2084 readbacks,1416 value comparisons/2832 readbacks,
849 contact readbacks/1024 fresh IDs and1251 FIND_NODE comparisons/2502 readbacks.
Native tests cover flags/max shadowing/wrapping, mode/null order, booleans,
source skew, legacy defaults/provider fallback/rewind, exact8192 budgets,
ownership/copy transitions and actual provider/thread failures.

The initial GCC Release build failed -Werror=maybe-uninitialized within the
inlined defaulted Decoded copy's nested std::variant/string/vector cleanup path.
Debug source/bounds/fresh Java comparisons had passed. The default copy
constructor definition is now out of line, preserving standard copy semantics
and all warning flags; new tests exercise both reply alternatives and assignments
between them. The same optimized GCC tree then built and passed focused checks.
The failed171-input source/log capsule is retained under before-copy-definition-
fix; final qualification uses entirely fresh v2 roots on all three toolchains.
No warning suppression, lowered optimization or golden mutation was used.
No native/source wire mismatch was found.

All prior packet/value/node/primitive and owned-UDP regressions remain passing.
Qt rows have17 and13 passes, zero skips; each compiler also passes40 FIND_NODE
and40 PING Java/native exchanges over160 IPv4 datagrams on new owned allowlisted
loopback ports. Java peers are connected/fenced source packet helpers; native
IPv6 remains separate from Java IPv4 socket coverage. No actual FIND_VALUE
network service or ordinary profile/plugin activation is claimed.

All lower155 qualified inputs remain byte-identical. Compiled/tool/fixture inputs,
source/helper/core hashes, commands/logs, OpenSSL3.5.5/Qt6.12 runtimes and binaries
are frozen in matrix.json. Leak detection remains enabled without suppressions.
No independent reviewer was used. Original Java HEAD/index/running program/live
profile remain untouched. Immutable implementation hashes follow in a later
provenance commit.

Next: owned typed continuation delivery, retaining each part until final or
terminal error without moving the callback away on the first packet; actual
anti-spoof authority, lookup/routing/storage/persistence and host/plugin services
remain. No operational DHT, complete client/plugin release or measured whole-
client performance claim is made.

Final source readback totals: 2224 accepted, 126 matching failures/compiler. Qualified inputs: 171.
