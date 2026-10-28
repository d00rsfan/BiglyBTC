# Original Azureus position wire compatibility review

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The guarded helper copies
exact original DHTUDPUtils framing and DHTNetworkPositionManager.deserialise
bodies, supplies provider registration/reply fields/no listeners and counts
debug errors. Actual unchanged frozen source-built Vivaldi v1 Provider/Factory/
Impl and coordinate holder classes perform float input/output. No normal position
manager/core/plugin/config startup or coordinate estimation/update/randomization
is run. The frozen core class digest is checked before and after readback.

8554 observations comprise1508 encode operations and7046 parses. They cover old
versions10-14, generic15+, optional51+, null/empty arrays, fallback NaN points,
null-before/after-first-v1 partial failures, unknown provider types, signed size
bytes, count narrowing through257 entries, registration absent/present, known
provider reads independent of declared size, failed provider read/reset then skip,
representative truncations, signed zero/infinities and NaN bit patterns.

All native observations match the bounded fixture domain. Canonical owning v1
wire data preserves Java DataOutputStream's NaN encoding. The original manager
reset after provider failure and signed negative unknown-size no-skip are kept,
including old-format null results. Raw accepted framing is not evidence of a
valid coordinate, transaction or peer. Host coordinate validation/update policy,
provider/listener lifecycle and algorithms are pending.

Native tests check8192-byte input/output and bounded host scans, exact caps,
owning copy/input independence, provider failure/reset/registration, signed skips,
null serializer partials, fallback NaN, legacy projection failure and count-wrap
tail ownership. Failed partial outputs must never be sent. The enclosing transport
also needs the whole header/body packet budget. Oversized host serialization
outside these bounds is not claimed to match arbitrary original output.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan builds pass both CTest rows and
2494 original encoder comparisons/4988 accepted-failure Java readbacks each.
1024 fresh IEEE vectors supplement the frozen corpus. Commands and exact source,
method, class, fixture, log and binary hashes are recorded. Leak detection stays
enabled without suppressions. Initial local bounds compilation found two test
hex-literal boundaries, fixed by separating adjacent string literals before fresh
qualification. No native fixture mismatch or sanitizer failure occurred. No
independent reviewer was used.

Alternative reply contacts, PING packet assembly, UDP transactions, routing/contact
identity, storage/persistence and plugin/runtime activation remain required. This
is a shared local wire component, not an operational DHT or plugin release. No
full integrated baseline/release gate is advanced. A subsequent commit records
the immutable implementation hash after its existence can be verified.
