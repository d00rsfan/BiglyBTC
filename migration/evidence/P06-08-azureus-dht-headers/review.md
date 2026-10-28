# Original Azureus UDP header compatibility review

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, original PRUDP and
DHT request/reply, endpoint/length, network and protocol-version methods. The
reference copies exact method bodies into a guarded helper with supplied
transport/contact getters, registered network, clock and transaction ID. It does
not instantiate the normal Java transport/manager, read configuration, initialize
CoreFactory or open sockets/DNS/user profiles. Original base serializers are
included; transaction scheduling/random generation is not.

7379 original observations cover3840 minimum-version policy values,1646 emitted
headers and1893 parsed headers. Signed-byte protocol negotiation, all network
contexts, wrapped override values, IPv4/IPv6/mapped/unresolved addresses, unsigned
ports, connection/transaction/instance limits, signed vendor/flags, wrapped clock
skew, short processing time, truncation/consumed offsets and opaque bodies match
the recorded fixture domain. Foreign vendor/originator fields and received reply
processing time are deliberately rewritten by the original object serializer.
Native raw packet/body ownership preserves the original input independently of
that header-only projection. Mapped IPv6 normalization shrinks the projection,
without changing the original consumed header offset or moving the opaque tail.

Native bounds tests check8192-byte total packet limits, endpoint width failures,
version-before-network ordering, skew diagnostics before truncated flags, wrapped
timestamps, caller/copy/move ownership, moved-from rejection and projection/input
independence. The8192-byte cap matches original PRUDP maximum. Arbitrary oversized
Java serializer acceptance is outside the native domain; normal DHT1400-byte
packet budgeting belongs to the future transport. Incoming action values remain
opaque and do not authorize dispatch or prove transaction/peer identity.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan builds pass both CTest rows and
3292 source-built Java readbacks per compiler:1646 native emitted packets and1646
canonical header projections. Legacy/negative versions use explicit wrapped
minimum128 in readback, rather than silently bypassing a production check.
Reference class/method/source hashes, native compiled inputs, fixture bytes,
commands, logs and binary hashes are recorded in matrix.json and per-compiler
readback records. Leak detection remains enabled, with no suppressions. No
independent reviewer was used.

The initial corpus comparison and local bounds/readback checks also pass in
/tmp/biglybtc-azureus-headers-dev-gcc. Two earlier source-helper scaffolding compile
failures (unexpanded Java brace and supplied base-field name) remain in generated
temporary roots; they were fixed before characterization and are not native test
failures. Qualification uses a fresh guarded helper and fresh build directories.

PING/other payloads, UDP transaction/multiplexing/lifecycle dispatch, routing,
contact identity, persistence, store/lookup services and full plugin integration
are pending. This is a local shared transport foundation, not an operational DHT
or a binary release. Native implementation provenance is recorded by a later
commit; no whole-component integration baseline or release gate is advanced.
