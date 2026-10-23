# Original Azureus PING request options

C++23 wire codec for original BiglyBT core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This is shared DHT groundwork for
ratings, related-content and message-sync, separate from Mainline DHT. It starts
no listener, Java core/plugin, DNS, discovery or telemetry collection, and reads
no profile. It has no Qt or JVM runtime dependency.

`encodeOptions` borrows version/network, alternative network/count spans and an
optional already ranked cached upload snapshot. It returns owned bytes or a
failure with the original partial serializer bytes for diagnostics. Failed
partial packets must never be transmitted. Null arrays, low-byte narrowing,
16-network bounds, extra-count handling, version/network gates, the no-stats
sentinel and the original 24-country output budget are preserved.
`encodeUpload` exposes the same supplied-snapshot/no-stats section for complete
request/reply assembly; its caller applies the version/network gates. P06-12
requalifies this added API alongside the original request wrapper.

Global statistics sampling/ranking, the 60-second cache refresh and shared
333ms emission throttle remain host work. The reference supplies cached arrays,
totals and clock to the exact original serializer method and traps any attempt
to fetch CoreFactory/global statistics. Native callers choose a snapshot or
explicit no-stats output. This does not claim the complete statistics policy.

`decodeOptions` returns owned pairs/statistics/trailing bytes and the exact
consumed offset. Required alternative-request truncation fails the request.
Optional upload-stat truncation/malformed data returns no stats after the same
consumption as Java, without turning that optional field into a request failure.
`decodeUpload` also exposes this bounded decoder independently. Inputs are
limited to the original PRUDP maximum of 8192 bytes; the enclosing transport
must additionally enforce its full datagram/header budget.

Legacy formats 0/1 keep array order and duplicate entries. The original format0
int mask does not make its counter unsigned; signed widening is retained.
Version2 is an owning semantic country map, including totals under the empty
key and last updates per country/direction. The source helper canonicalizes
HashMap iteration order for comparison. No Java array ordering is promised for
that map. Signed-byte UTF-16 country conversion and X0/I2P, X1/Tor mappings match
the original. Java float-to-long saturation/NaN and long-to-float ties-even wire
conversion avoid undefined casts and dependence on native rounding mode.

CTest compares 12,772 original observations and checks ownership, body limits,
partial failure consumption, country semantics and four native rounding modes.
Migration qualification adds 3,288 original serializer comparisons/request
readbacks and 2,802 original upload-block readbacks per compiler. These include
2,422 fresh signed64 edge/random and UTF-16 inputs beyond the frozen corpus.
Exact method/source/class/fixture/binary hashes are recorded in P06-09 evidence.

The reference uses exact original PING request constructor/serializer bodies,
with a no-op base header and supplied immutable context. The separate qualified
header codec must be joined by the future transport. PING reply Vivaldi/contact
extensions, UDP transactions, routing/contact identity, storage/persistence and
plugin/runtime activation remain pending. This library is not an operational
DHT or a complete plugin release.
