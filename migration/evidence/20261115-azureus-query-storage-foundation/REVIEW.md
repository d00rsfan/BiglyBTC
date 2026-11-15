# Query-storage wire and database prerequisite

Native `90aa3c49107c6a6c9e8f5fc53a18195b8c31e31f`, tree `19f66998511a47e50e5da15206f25b2e7affe139`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The actual original request/reply serializers produced 34 generated packets at
protocols 51 and 55. All 24 reply cases were also decoded and reserialised by the
actual original reply class. Native decode/encode matches every complete packet
byte for byte, preserves trailing bytes independently and refuses every strict
packet prefix. Negative counts, invalid widths, excessive zero-width expansion,
network/version mismatches and finite packet limits are covered. Original Java
uses `size+7/8` for the response bitmap: its wire length is the entry count. The
native encoder preserves spare zero bytes; the decoder ignores their unused bits.
No random STORE token is added: the original field is not on this packet's wire.

The actual original DB produced 18 prefix-map transitions in both insertion
orders, with its actual XOR-distance comparator. Native follows six-byte prefix
lookup and two-byte key tails. Collision winners use distance from the actual
bound local ID. Removing a winner does not silently restore a previous loser.
The index is maintained on local/remote record creation, removal, cache purge,
expiry and close; query observation has no ordinary read, clock or diversification
counter effects. Newly emptied mappings survive until the next expiry scan, as
in Java. Limits retain the existing DB key bound and at most 8192 query rows.

Reference helpers use actual original classes from the recorded baseline and a
generated JVM profile. Packet construction supplies fields via reflection/Unsafe
without creating a transport or opening sockets; reply parsing uses the actual
original parser. The DB helper reuses the prior actual cache-reference harness.
The rejected protocol-50 fixture was corrected to the original minimum 51. Two
initial native test expectations were corrected to original header acceptance
and two-scan expiry semantics; production behaviour was not weakened.

Focused GCC16/Qt6.12.0/C++23 build and all selected tests passed without skips;
see [qualification.json](qualification.json). No application build was repeated
at this prerequisite, no daily image was prepared and no live profile was read
or changed. Network actor integration and explicit replication surveys remain
in progress; this is not completion of F09.
