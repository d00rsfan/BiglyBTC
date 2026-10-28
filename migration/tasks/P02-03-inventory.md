# P02 original download inventory reader

- Parent P02-03; F05.inventory_read, F25.compatible_inventory; state in_progress.
- Source BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  GlobalManagerImpl.loadDownloads/loadDownload and saved force/pause restoration.
- Owner Codex; self review only. Core-only immutable typed snapshot, no Qt/engine.
- Writes native core/tests and migration records only; fixture files under fresh
  `/tmp/biglybtc-*`. No normal profile/payload reads, startup or writes.

Read list and legacy map inventories, original torrent/save paths, optional binary
hashes, persistent/force flags, original/startup states, counters and pause owners.
Keep root/source indices for later edits; unknown fields remain in the original
document. Surface malformed entries as explicit indexed issues while retaining
valid entries. Do not silently drop non-persistent saved state or normalize user
paths/unknown values. Bound counts and reject NUL paths before any filesystem use.

Verify current/legacy state and pause encodings, malformed fields, byte hashes,
limits and owned snapshot lifetimes, then compare actual generated guarded Java
profiles. This reader is not a complete resume engine, file-link importer, queue
policy, settings migration or permission to start a normal user profile.
