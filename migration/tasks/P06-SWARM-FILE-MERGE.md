# Swarm file merge in the current preview

Status: explicitly authorised by the user on2026-11-13. Deliver with the current
resize/properties/graceful-close candidate, then STOP. Preserve running Java;
no automatic original-profile launch or writes by development tools. Embedded
browser/player skipped, IP-filter low. No new agents. Keep compiler logs local,
use focused tests and one coherent integration build per stage.

Baseline: BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
core/src/com/biglybt/core/global/impl/GlobalManagerFileMerger.java,
DownloadManagerState.AT_MERGED_DATA and original Files configuration defaults.
Original settings: Merge Same Size Files=true; Extended=false; Tolerance=0;
Merge Same Size Files Min Pieces=5. The persistent attribute is `mergedata`
(single d), distinct from the `mergeddata` table-column ID. Never rewrite or
invent a different compatibility key.

1. Qualified local-reuse policy/index: original enabled/extended/min-pieces/name/
   length-tolerance/Merkle-root selection, nonzero non-padding files, distinct
   torrent identities, original skip/pause/low-noise/metadata restrictions.
   Bounded incremental indexing, not O(library-squared) polling or new discovery.
2. Retained-owner data path: existing Host worker and sole SDK alert consumer;
   source/target exact admission/epoch, owned async read buffers, bounded jobs,
   correct original/SDK file geometry. SDK read_piece requires HAVE; do not ask
   it for missing target pieces. For partial boundary assembly, use authenticated
   original-storage reads and validate the assembled target hash before submission.
   Do not overwrite verified target pieces or bypass stopped/recheck/paused state.
   Implement v1/hybrid and qualified v2 piece/root verification, including padding
   and short final leaves. Missing authenticated v2 layers must stay unavailable.
3. Hash mismatch/tolerance/offset handling: candidate similarity is not proof.
   Validate complete target piece; bounded mismatch/backoff and cancellation.
   No hash/passkey/filename publication, additional tracker announce or DHT query.
4. Accounting/checkpoint: local bytes do not become peer/global payload traffic.
   Preserve original mergedata prefix and opaque fields; update source-owned
   buffered/checkpoint state, never stale live attribute writes. Distinguish local
   submission/verification observations; deduplicate across callbacks/restarts.
5. Native controls/presentation: compatible settings through the existing config
   service, per-torrent merged data and actual file-merge activity/status, useful
   original column projection. No fake enabled placeholder or unsupported option
   pretending to work. UI work must not undo window-resize/table stability fixes.
6. Focused generated tests: distinct info hashes/same local content, complementary
   partials, mixed file offsets/boundaries, v2/hybrid/tail/padding, mismatch causes
   no unsafe writes, skip/pause/stop/recheck/session fencing, no duplicate local
   credit or network counters, normal close/reopen/Java-compatible resume/attributes.
   Private-key/public-discovery boundaries remain unchanged. Public evidence uses
   only generated fixtures, immutable source/native hashes, actual test results.

Prerequisite current fix: bounded32 graceful retirements, caller-sized close
budget (not a universal5s for all checkpoints), final counter certification bound
separate from filesystem work, visible warning for actual checkpoint failure.
Observed old-profile maps remain untouched by tools; no historical snapshot restore.
Slow startup is still open and must remain recorded; this chapter is not a claim
that the whole migration/advanced compatibility/performance epic is finished.
