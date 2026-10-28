# P01 engine comparison: original storage layouts

- Parent P01-02; F02.compact_map, F02.reordered_map, F05; state in_progress.
- Owner/integrator Codex; self review only. No production engine selection.
- Frozen BiglyBT source: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
  FMFileAccessCompact, FMFileAccessPieceReorderer, FMFileAccessController,
  FMFileManagerFactory/FMFile and TOTorrentFactory.
- Inputs: source-built Java core whose class digest matches P00-03.
- Writes: `migration/fixtures/java/StorageOracle.java`, storage proof/tool/tests,
  provenance/evidence and newly created `/tmp/biglybtc-*` fixture directories.
- Never inspect or modify the running application, normal profile or payloads.

Generate real compact and piece-reordered payload/control files through the
original FMFile API. Characterize aligned/unaligned multi-file boundaries, short
files, absent pieces and actual logical/physical offsets. Reopen with Java and
read each logical piece independently; absent storage is explicitly transient,
never trusted as a verified zero piece. Preserve all generated originals.

Implement a bounded immutable mapping proof, not a second torrent engine. Reject
invalid maps, out-of-range offsets, overflow, duplicate physical allocations and
unsafe size counts. Compare mapped logical reads to Java, retaining holes as
missing. Verify both compiler builds and sanitizers. This does not yet implement
new reordered allocation, piece relocation, crash-safe control writes, custom
engine disk I/O, file links, or complete storage compatibility; those remain
explicit integration tasks before AD-001/full switching can close.
