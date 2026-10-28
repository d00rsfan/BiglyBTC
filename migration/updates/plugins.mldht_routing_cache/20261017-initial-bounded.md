# Initial bounded Mainline routing-cache decoder review

Self review, 2026-10-17. Baseline original engine fork
`f9290cbd63805229aef418639b781696c5fa1573`: Node table format,
KBucketEntry.fromBencoded/toBencoded and AddressUtils packed endpoints.

The native host adapter decodes main/replacement entry lists,20-byte node IDs,
6/18-byte endpoint encodings including Java IPv4-mapped normalization, signed
timestamps, Java int32 truncation/Math.abs (including MIN_VALUE), verified flags,
optional binary versions, root oldKey/timestamp and raw IEEE754 estimate bits.
Nonmap list items are ignored as in original list streams. Unknown fields and
complete original bytes remain owned by the immutable core document. Borrowed
version views survive moving the cache object. No editor, disk replacement,
network admission or DHT startup exists in this bounded adapter.

Original fixtures use unchanged bencode and Key/Prefix helper sources, with exact
entry/address/typedGet method bodies and constructor body. A supplied clock and
host fields replace unrelated engine services. This is not a complete operational
Node.loadTable or routing oracle. Frozen source/method/harness/class hashes and
ReferenceFence protect the local generated-data reference workload.

64 observations include59 accepted and5 rejected caches, defaults/wrong optional
types, binary versions/opaque fields, mapped IPv6, port0/65535, signed overflow,
negative zero/NaN/infinity payload bits and invalid required node/address fields.
GCC16/Clang24/ASan+UBSan+LSan each pass original cache fixtures plus two existing
core-format regressions.59 accepted cache files per compiler also pass native
preserved-byte output -> frozen original Java decoder with identical model/bytes.
`cache-matrix.json` and the three `*-cache-roundtrip.json` files pin scope,
commands, input classes, sources and actual binaries. Leak detection remains on.

The parser bounds input/string data at50MiB, nodes at2million and decoded entries
at100000, and rejects noncanonical/ambiguous bencode via the existing core parser.
These limits/errors preserve source files; they are not a claim of accepting every
malformed or arbitrarily large Java input. No full application matrix is implied.

Original files are named from DHTtype.shortName, `IPv4-table.cache` and
`IPv6-table.cache`; do not lowercase them. Actual host file discovery, original
plugin-folder/config/data binding, routing insertion/splitting/identity/eligibility,
cache updates, network/privacy policy and actual engine pairing remain required.
Engine-derived adapter files retain MPL2; the full notice is stored beside them.
