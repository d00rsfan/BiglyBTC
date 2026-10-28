# Original Azureus alternative-contact wire review

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The guarded helper copies
exact original contact/array/length/byte-array/constructor/getter bodies and
supplies a monotonic clock. Actual frozen BEncoder/BDecoder prepare outgoing
generated dictionaries. Incoming getProperties is trapped and never called:
raw-property framing, age and duplicate-detection identity are this task's scope.
No normal DHT/core/config/plugin startup, network/DNS or user profile is involved.

5636 observations contain1995 encode operations and3641 parses. They cover signed
int/short/byte edges, null/empty/64/over-count arrays, unsigned network/version
getters, all256 property byte values, arbitrary raw incoming properties, signed
Arrays.hashCode wrap, negative age normalization and int32 monotonic-seconds/age
wrapping across negative/positive int64 clocks. Partial fixed fields fail with
source consumed offsets. Property short reads preserve zero padding, including
declared65535 fields, because the original ignores a single read's return count.
Tails and hashes include those exact padded bytes. All bounded native observations
match the fixture domain.

Native tests check8192-byte input/output limits, count/field limits and partial
output diagnostics, raw input/copy/move independence, cleared moved-from identity,
clock/age wrap, exact caps, zero-padding hashes and successful expansion at the
input cap. Total decoded property allocation is bounded by8192+65535 before each
allocation. Failed serializer output must never be sent. Array helper64 and the
future PING reply16-contact selection are distinct policies.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan builds pass both CTest rows,
2506 original encoder comparisons and2509 Java readbacks per compiler.512 fresh
generated maps supplement the frozen corpus, and three readbacks cover padded
expansion, including an8192-byte input with a65535-byte final field. Frozen core
classes are verified before/after source readback; exact source/method/class/
fixture/log/binary hashes and commands are recorded. Leak detection remains
enabled without suppressions. Initial local fixtures, bounds and readback pass;
no native mismatch or sanitizer failure occurred. Copy assignment uses a temporary
owner before changing fields to preserve hash/byte coherence on allocation error.
No independent reviewer was used.

Outgoing snapshots require already encoded host dictionaries; this wire layer
does not perform the original lazy tolerant BDecoder projection/canonicalization.
Only generated valid map framing is compared with BEncoder here. Incoming bytes
never become endpoints, trusted overlay settings or an authenticated identity.
Lazy map behavior, endpoint/overlay handling, reply16 selection and full PING
assembly, UDP transactions, routing/contact lifecycle, storage and plugin/runtime
activation remain required. No full integration baseline or release gate is
advanced; immutable implementation provenance follows in a later commit.
