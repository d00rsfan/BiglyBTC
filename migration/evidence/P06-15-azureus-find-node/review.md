# Original Azureus FIND_NODE packet review

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original
request/reply constructor and serializer bodies run through one stream over the
qualified header/position/upload and node/contact helpers. Nested contact types
and the supplied inert transport bridge adapt helper namespaces only. Clocks,
network, IDs, providers and cached statistics are supplied. Contact construction
has empty provider state. Unchanged frozen pure classes and ReferenceFence are
verified; no ordinary manager/core/transport/plugin/profile or network starts.

Requests preserve0..64-byte targets, signed-byte version comparisons, status/size
at22 and BiglyBT network4 uploads at55. Source single-read target zero padding,
partial required-int EOF, missing interim upload blocks and swallowed malformed
optional blocks preserve exact consumed positions. Replies preserve random ID
at7, status at12/default-1, size at13, Vivaldi at10, and regular contacts at all
versions. There is no PING first16 clamp. Target null/65-byte and contact null
errors preserve preceding partial encoder bytes as diagnostics, never sendable
packets. Header/action/minimum/network/skew and position reset/body cursor order
are preserved. Random IDs are wire snapshots, not active nonce authorization.

Decoded packet, target, positions, contact IDs and tails own storage. Copy/move
and self-move behavior is exercised; moved-from views/offsets clear. An explicit
borrowed owner-thread128-entry cache performs contact hashing before port0
filtering. Later malformed contacts preserve earlier history; crypto and wrong
cache-thread failures retain global positions without invented IDs. The whole
packet cap8192 includes headers/fixed fields; short hostile counts cannot cause
large preallocation. Returned endpoints remain inert, without routing activation.

Fresh GCC16, Clang24 and ASan+UBSan+LSan pass all15 combined CTest rows,6543
original observations (874 encodings,5668 decodings,1 cache reset),1251 source
serializer comparisons (including512 fresh vectors) and2502 Java readbacks per
compiler. Readbacks include2442 accepted and60 source/native matching failures.
Native tests additionally cover ownership, defaults,17 contacts, mapped offsets,
partial target/int/stats, network/action order, provider reset, cache/provider/
thread failures and8192-byte budget. Existing node849 Java readbacks/1024 fresh
IDs and all primitive/Qt regressions pass. The Qt row has17 passes/no skips; each
compiler also passes40 actual owned IPv4 Java/native exchanges over80 datagrams.
Native IPv6 remains separate coverage from Java IPv4 socket interoperability.

Initial scaffold errors (missing byte-array bridge, imported OpenSSL CMake scope,
ambiguous read/write integer overload) were fixed before qualification. The first
golden replay caught fixture-driver omission of Java factory normalization for
mapped IPv6 contact snapshots; fixed in that driver without altering the codec
or original goldens. Failure logs remain under development. A post-matrix
metadata check caught standalone Z incorrectly counted as0 by a prefix-with-space
counter. The collector now counts1; regeneration proved all commands, expected
observations and emitted packets byte-identical. The same three qualified binary
fixture tests pass with the corrected manifest. Original matrix/manifest and
explicit correction hashes/commands remain in the evidence. No native binaries
changed for that metadata correction.

Exact117 inputs, source/helper/core hashes, tool commands/logs, OpenSSL3.5.5 and
Qt6.12 runtime hashes and binary hashes are recorded. Leak detection is enabled
without suppressions. No independent reviewer was used. Original Java HEAD/index,
running application and live profile remain untouched. Immutable implementation
hashes follow in a later provenance commit; the full upstream integration baseline
is not advanced for this bounded partial transport work.

Next: typed owned-loopback FIND_NODE exchanges sharing the existing bounded PING
actor, then lookup scheduling, anti-spoof lifecycle, routing/store/persistence and
plugin/host activation. No operational DHT, whole-client parity/performance or
native binary release is claimed.
