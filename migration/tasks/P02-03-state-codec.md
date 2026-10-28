# P02 compatible compressed state and torrent envelopes

- Parent P02-03; F01.gzip_torrent, F05.state_cache_gzip; state implemented (uncommitted).
- Source BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  DownloadManagerStateImpl.loadGlobalStateCache/saveGlobalStateCache and
  TOTorrentDeserialiseImpl constructors. Owner Codex; self review only.
- Real guarded Java engine fixtures show plain `active/HASH.dat` torrent/state
  documents and gzip `active/cache.dat` containing cached attributes. Java also
  accepts gzip torrent files. Earlier synthetic state fixtures did not cover
  compressed envelopes or operational cache coherency.
- Writes: native state-codec adapter/tests and migration tools/evidence;
  generated `/tmp/biglybtc-*` fixtures only. Core bencoding stays standard-library
  only; installed zlib1.3.1 is an independently recorded development input.

Bound inflation, member count and output allocation; reject truncated streams,
bad CRC/trailers and malformed decoded bencoding. Preserve the original gzip
bytes exactly when no edit occurs. For a deliberate integer edit, preserve all
other decoded bytes and produce a Java-readable gzip stream. Exercise compressed
torrent hashes and real generated cache files with both compilers/sanitizers.

This codec does not itself solve cache coherency. Before a native torrent-state
write, invalidate/update the corresponding cached attributes durably, then save
the original-compatible state. Crash ordering and actual Java reload remain
separate integration gates. Never overwrite an unparsed opaque cache.

GCC and Clang pass native malformed/CRC/quota/member tests plus guarded Java
config edits and unchanged v1/v2/hybrid hashes. Current Clang ASan/UBSan/LSan
passes both focused suites. Real generated inventory, per-torrent state and
compressed global cache also round-trip byte-for-byte. Native cache coherence
and writable operational session switching remain pending.
