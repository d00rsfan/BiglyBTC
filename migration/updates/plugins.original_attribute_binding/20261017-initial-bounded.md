# Original plugin attributes/full64 settings review

Self review, 2026-10-17. Core baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: cached-state attribute getter,
TOTorrent map-property getter, DownloadManagerState long getter, plugin attribute
prefix statement and configuration Long getter/raw typed fallback.

Native snapshot lookup uses Plugin.<runtime-id>.<name> for torrent attributes;
configuration retains the original external-directory-derived prefix. A cached
attribute dictionary replaces the whole primary map, even when empty. Missing or
wrong-typed primary map properties become empty; wrong-typed cached maps and Long
attributes return an explicit error. Full64 configuration keeps original Long
values/default/diagnostics; existing int32/boolean conversion is unchanged. Both
readers now share one ISO-8859-1 key lookup implementation; no key repair occurs.
All input documents/raw/opaque bytes remain unchanged and no I/O is performed.

The fenced helper compiles exact original getter bodies/prefix statement and
uses supplied maps/defaults/monitors, frozen original bencode and synthetic names/
values. 952 observations (864 attribute plus 88 configuration) include 208 original
type failures, empty-cache precedence, missing/wrong-type maps, large signed
values and UTF8/legacy raw ISO keys. GCC 16, Clang 24 and Clang 24 ASan/UBSan/LSan
pass those plus 90 earlier settings snapshots, 64 routing-cache cases and existing
bounded core/document regressions. Matrix pins full source/method/helper-class/
fixture/binary hashes. Leak detection remains on; no whole application claim.

Bounds: valid supplied primary dictionary, optional cached entry dictionary,
runtime ID 1..64 bytes and attribute name <=4096 bytes. Full cached-wrapper
construction/metadata validation, cache entry/hash index, lazy cache-only loading
when a primary file is missing/damaged, file discovery/ownership, attribute edits,
counter/ID journal and actual host RPC binding remain required. This is a pure
snapshot interface, not a complete state-file loader. GPL2-or-later derived code.
