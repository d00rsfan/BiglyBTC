# P02 compatible torrent-state ownership and durable writes

- Parent P02-03; F05.state_ownership, F25.compatible_state_write; state in_progress.
- Frozen BiglyBT source `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  DownloadManagerStateImpl.CachedStateWrapper.fixup/loadGlobalStateCache,
  GlobalManagerImpl.loadDownloads, TorrentUtils.readFromFile and
  TOTorrentImpl.serialiseToBEncodedFile.
- Owner Codex; self review only. Linux bounded foundation, not a complete engine.
- Writes native adapters/tests, migration records and fresh generated `/tmp`
  profiles only. No actual profile, payload writes, public peers or live app use.

Use the shared profile lease and securely opened `active` directory. Load plain
or gzip `HASH.dat` with original-compatible `.bak` fallback. Keep original bytes
for stale-write checks. Restrict state edits to resume/attributes/Azureus property
sections; torrent info must remain unchanged. Save with fsynced temp/backup and
atomic replacement that never removes the only primary. Java ignores `.saving`
in this path and falls back to `.bak`, so configuration's primary-to-backup gap
must not be copied into torrent-state writes.

Before mutating an active torrent, hydrate cached attributes/azp/azpp exactly as
the original CachedStateWrapper does, then durably remove that torrent's entry
from the gzip cache while retaining other entries/unknown data. Commit new resume
state only after that removal is durable. This avoids opening every inactive
torrent merely to invalidate a global cache. Corrupt cache recovery must retain
its original bytes and match Java's fallback to individual state files.

Test normal writes and each crash boundary with Java's actual torrent/backup
reader, original info/unknown data retention, stale/symlink/path rejection and
cache conflict/hydration. Process-crash tests are not power-loss proof. This does
not yet implement trust/import of piece flags, payload flush ordering, compact
allocation, file links or the complete Java/native/Java session acceptance case.
