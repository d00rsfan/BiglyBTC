# Owned STORE and terminal ERROR integration review

Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The retained source
audit pins twelve original files and thirty-three method bodies, including
STORE batching, transport contact admission, reply dispatch and PRUDP finality.
This component implements one owned physical STORE datagram and detailed remote
ERROR delivery. Logical STORE chunking/first-packet completion, retries, priority,
external address updates, key-block trust and host/router/DB effects remain
pending. Inert supplied server snapshots have no authorization meaning. Actual
P21 authority/P22 admission serving is the next separate integration.

STORE owns keys/value sets until synchronous encoding completes. Borrowed views
never escape that call. Independent counts are preserved; native limits are255
keys/sets,256 values,255-byte keys,512-byte values and1400 encoded bytes. The
packet codec's separately qualified8192-byte bound remains unchanged. Detailed
PING/FIND_NODE/FIND_VALUE/STORE outcomes own either their expected packet or
ERROR1032; old callback signatures adapt ERROR to appended Failure::Remote.

Endpoint, transaction, connection and expected normal action or ERROR bind before
body parsing. Network/version and fixed expiry checks apply before and after
decode. Foreign/malformed errors never retire valid pending work or warm contact
bodies. All error types are terminal, including after FIND_VALUE parts. Earlier
queued parts stay ordered, with the existing shared callback state and bounded
pending/completion capacity. Dead receiver, owner movement, deletion, cancel,
close, timeout, reentry and exceptions retain their qualified lifecycle rules.
Decoded error fields may include source-padded65535-byte signatures: event count
is bounded separately from encoded datagram size. No error field is trusted as
an address update, signature grant or authorization decision.

Fresh GCC16 and Clang24 Release roots and a fresh Clang24 ASan+UBSan+LSan Debug
root each pass all26 combined CTest rows and73 Qt passes, zero failures/skips.
Actual connected source-Java/native traffic adds20 protocol-context/server-mode
pairs,120 native requests/160 callbacks and80 Java reverse requests,440 new
datagrams per compiler. Forty ordered value continuations precede terminal
errors. Earlier PING/FIND_NODE/FIND_VALUE interoperability remains passing,
bringing combined actual traffic to760 datagrams per compiler. Java sockets are
IPv4 only, explicitly fenced to newly owned numeric loopback peers. Native IPv6
is covered separately. ERROR9209, FIND_VALUE2350, STORE2084, values2832,
nodes849 plus1024 fresh IDs and FIND_NODE2502 source readbacks also pass.

The new source peer reuses immutable qualified packet/header/value/node/error
helpers, verifies their committed reference bytes and rejects inconsistent
duplicate binary class names. No old helper is edited or rebuilt; original core
class digest and ReferenceFence stay unchanged. Native/source/driver tasks were
split across three user-authorized workers, cross-reviewed, then integrated and
qualified by root. No separate full-client parity reviewer is claimed.

Two development problems are retained. The first combined run exposed an old
PING test's unsupported-action byte1027, which became a supported STORE action.
Only that test input changed to still-unsupported1033; its assertions remain.
The sanitizer build then hit tmpfs user quota while creating a static archive,
and the first retry hit the same quota near the final executable links.
Both failed logs and all pre-retry reports are preserved. Two cleanup manifests
cover346 reconstructible object files from twelve exact older generated roots,
reclaiming520648848 bytes and verifying307 retained executables/libraries. Narrow
AST recovery tools remove only the already-created-root guard and reuse the verified
source reference, re-running complete commands in the same initially fresh roots.
GCC/Clang builds are incremental no-ops; sanitizer compilation resumes. Compiler
flags, optimization, source, fixtures and leak detection are unchanged. Input
snapshots are explicitly captured at recovery entry rather than represented as
persisted before the original quota failure. Recovery/cleanup manifests and all
test/peer/readback logs retain the complete evidence trail.

All230 input hashes, both recovery tool hashes, commands, binaries, Qt/OpenSSL runtime,
frozen Java helpers and source audit are recorded. Original Java HEAD/index,
running client and live profile remain untouched. This is a bounded generated
transport component, with no production DHT/plugin/app activation, release or
whole-client performance claim. Continue real authority/admission, routing,
storage/persistence and dependent host/plugin work.
