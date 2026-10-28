# Original Azureus PING request options review

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original request
constructor/serializer, alternative-request length/array helpers and upload-stat
methods run in a guarded source-built helper. The base header is a no-op; cached
country arrays/totals, clock, version and network are supplied. The unchanged
serializer stays inside the supplied cache or throttled no-stats path. CoreFactory
and global-statistics access are trapped. There is no normal core/transport/plugin
startup, sampling/ranking, network, DNS, profile or telemetry collection.

12,772 observations contain942 original encode operations,9055 option parses and
2775 direct optional-stat parses. They cover protocol/network gates; null, empty,
extra/short array pairs;16-network count limits; low-byte narrowing;24-country
budget division; all256 byte values in country output; unusual UTF-16 strings;
signed64 extremes; old0/1 and current2 statistics; duplicate updates; unknown
versions, invalid counts and every truncation of representative bodies. All match
the bounded native fixture domain. There is no blanket malformed-input rejection
claim: the original optional-stat parser swallows failures after partial reads.

Legacy formats preserve their ordered array/duplicates. Version2 exposes an owned
semantic country map, matching the source HashMap contents; source output is
canonicalized by country in the helper. Java HashMap iteration order is not a
native API contract or claimed parity. Signed byte-to-char country conversion,
X0/I2P and X1/Tor mapping, signed legacy int widening (the unsuffixed mask is int
-1), float-to-long saturation/NaN and last direction update are preserved. Native
integer-to-float output uses explicit ties-even bits independent of native fenv.

Native tests check8192-byte bounds before parsing, input/return ownership, gates,
partial required/optional failures, unknown-version tail offsets, partial output
diagnostics, country semantics and all four native rounding modes. Failed encoder
output remains a diagnostic and must never be sent. The enclosing transport must
enforce the full header-plus-body datagram cap as well.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan builds pass both CTest rows.
Each also passes3288 exact original encoder comparisons/request readbacks and2802
original upload-block readbacks. These include2422 fresh long-edge/random and
UTF-16 inputs beyond the frozen corpus. Class/method/source/fixture/log/binary
hashes and commands are recorded. Leak detection stays enabled without
suppressions. Initial local qualification had one bounds-test compilation error
comparing expected<T,E> values without an Error equality operator; it was fixed
by comparing successful payloads before fresh matrix builds. No native fixture
mismatch or sanitizer failure occurred. No independent reviewer was used.

Global manager sampling/ranking/cache refresh, cross-packet333ms throttle, PING
reply Vivaldi/alternative contacts, complete header/body dispatch, UDP transaction
actor/routing, contact identity, persistence/storage and dependent plugin/runtime
activation remain required. This component is shared local groundwork, not an
operational DHT or complete plugin release. No integrated source baseline or
release gate is advanced; a later commit records immutable implementation hashes.
