# P02-07 Original plugin attributes and full64 configuration

Status: bounded read-only attributes/full64 configuration qualified across
GCC 16, Clang 24 and ASan/UBSan/LSan. Original core baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Plugin torrent attributes use runtime IDs: Plugin.<runtime-id>.<attribute-name>.
Configuration uses the external installation-directory prefix. Read cache
attributes before primary state, including an empty cached dictionary overriding
all primary attributes. Missing/wrong-typed primary map properties become empty;
wrong-typed cached maps and wrong-typed Long attributes fail as in Java. Preserve
all original bytes. Add full signed64 configuration lookup with original typed
fallback/diagnostics, retaining existing int32/boolean semantics and ISO-8859-1
key lookup. Use a shared key-lookup utility rather than two differing conversions.

The fenced reference uses exact cached-map/property/long-getter bodies and
supplied host maps/defaults/monitors, plus original bencode. No normal core/plugin,
real profile or I/O occurs. Actual profile snapshot ownership/cache indexing,
RPC provider, attribute/counter writes and cross-file journal remain required.

952 observations pass per compiler: 864 attribute/cache cases and 88 full64
configuration cases, including 208 original attribute type errors. The 90 earlier
settings snapshots, 64 routing caches and core/document regressions also pass.
Compiled shared lookup preserves original ISO-8859-1 key semantics. Actual cache
entry selection/metadata validity, cache-only lazy file loading and durable host
profile/provider integration are not claimed by this pure snapshot interface.
