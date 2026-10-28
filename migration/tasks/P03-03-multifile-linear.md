# P03 original multifile linear layout and continuation

- State in_progress; owner Codex; self review only.
- Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  TOTorrentCreatorImpl/TOTorrentCreateImpl/TOTorrentCreateV2Impl, DiskManagerImpl,
  DownloadManagerStateImpl, LinkFileMap and RDResumeHandler.
- Generated profiles, payloads and owned loopback peers only. No actual profile,
  public swarm, original running application or normal desktop interaction.

Characterize real original Java multifile v1/v2/hybrid metadata, pad-file/index
semantics, nested/non-ASCII names, empty files, per-file links and partial resume.
Use a piece spanning file boundaries. Save a real stopped partial profile and
retain its exact original inventory/cache/state and payload mapping.

Extend native linear mapping only after those cases resolve: map original file
indices to engine indices, validate all paths/policies before writes, preserve
already complete and incomplete filenames, flush before verified resume flags,
and complete each original file link without overwriting collision targets.
Prove original Java -> native -> original Java using the SAME generated files,
including crash recovery. Preserve the already-qualified single-file path.

Compact/reordered storage writes and skipped/priority policies remain distinct
pending gates; do not silently treat them as linear/default policy. This task is
not full P03 completion or a claim that every original library can activate yet.

Current observations: normal GCC v1/v2/hybrid same-profile continuation and
original Java reopening pass, preserving all three physical inodes, nested Hebrew/
Arabic names, empty files and original virtual-pad indices without creating pad
files. A first-alias interruption passes v1 Java-first recovery. The initial pure
v2 Java-first test reports 8/9 after recheck despite exact complete payload bytes.
The original-only complete-file run reproduces 8/9 without executing native code;
v1 and hybrid report all pieces. Evidence is retained under P03-03.

Independent source review now pins a candidate mechanism: the final piece map
contains 12,768 real bytes, 3,616 pad bytes and a zero-length trailing entry, while
DMCheckerImpl trims v2 padding only for exactly two entries. The source inference
and four original Git blobs are retained in
`migration/evidence/P03-03/v2-tail-source-followup-v1/`. A focused inert
counterfactual test remains required; no new cause experiment or fix is claimed. Native
completion now publishes verified flags after every descriptor is flushed and
before rename intents begin, retaining all old names. This is legitimate durable
completion, not trusting imported flags. Fresh crash/desktop/compiler/sanitizer
qualification is still required. An interruption before that commit, or a fallback
backup requiring Java recheck, retains the original v2 limitation; full recovery
parity is not claimed. Do not modify original production Java to hide it.
