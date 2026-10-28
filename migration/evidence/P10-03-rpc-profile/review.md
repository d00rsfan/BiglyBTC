# Persisted RPC profile reader review

Scope: read-only active/archive torrent Long IDs and the next-ID configuration
counter, borrowing the application's already-held profile lease. This is a
host-side data provider, not a registered/running xmwebui plugin or HTTP server.

Core source baseline: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
xmwebui source baseline: c9ab8f7cd62b2874f7385772992c0288a224e85d.
The original loadGlobalStateCache loop maps entries by hash with the last entry
winning. CachedStateWrapper.getAdditionalMapProperty returns cached attributes
before fixup; empty maps are authoritative. TorrentManagerImpl adds the runtime
ID to torrent-attribute names, while external plugin folder names define config
prefixes. ConfigurationManager.CONFIG_FILENAME is biglybt.config. Download
archive records are ordered stubs with attr maps in dlarchive.config.

The immutable decoded cache is shared out of readLibrarySource; readLibraryView
keeps its old signature and LibraryView keeps its old layout. The provider builds
one hash index and does not load primary state on its authoritative-cache path.
Other active rows lazily use the existing read-only TorrentStateStore::load,
including .bak recovery. It never calls beginUpdate, removes a cache entry,
allocates an ID, touches a payload or follows a saved external torrent filename.
Missing/malformed values carry diagnostics and remain unavailable. Wrong-type
counter values retain the original getter's default/diagnostic behavior.

Thirty-eight portable cases compare actual file bytes decoded by original
BDecoder and exact original getters. The source helper uses supplied lazy I/O,
default maps and monitors; it does not run full CachedStateWrapper construction,
normal startup, active registration or DownloadStubImpl restoration. Three
metadata-only original-generated v1/v2/hybrid seed sets are pinned independently.
Private keys, certificates and downloaded payloads are excluded from fixtures.
Native-only cases test unsafe config/archive paths, config backup recovery and
the combined 100000-identity limit. The tests repeat snapshots under one held
lease and check early cancellation plus file bytes, inode/mode, directory entries
and symlink targets. Cached primaries include missing/corrupt/symbolic/FIFO cases.

Runtime registration still determines the original active/archive/magnet
membership and order. Persistent inventory order alone cannot substitute for
GlobalManager.getDownloadManagers(), archived registration and visible in-memory
magnet rows. Consumers must establish that complete view and resolve diagnostics
before previewing or applying an identity ledger. No multi-file ID/counter writer
or recovery journal is introduced here. The provider is a separate static target
and is not enabled by the desktop host yet.

Review performed by the implementing agent; no independent reviewer was used.
Compiler/source/fixture/binary hashes and full test output belong to matrix.json
and the retained per-compiler logs. The missing-driver runner failure is retained
under rejected-missing-library-driver and is not a product failure or a pass.

Later tooling review removes the resume runner's successful-log skip: every
resumed build now replays its tests, so a prior success cannot certify a newly
rebuilt binary after code changes. The original qualified resume used unchanged
runtime inputs and remains pinned by its original immutable source capsule.
