# P02 compatible structured state edits

- Parent P02-03; F25.structured_edits, F05.resume_state; state in_progress.
- Source baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  BEncoder/BDecoder, DownloadManagerStateImpl.resume/attributes and global cache.
- Writes native core/adapters/tests and migration records only. No profile writes
  in this task; edited bytes go to generated test output until crash ordering and
  cache invalidation are qualified. Owner Codex; self review only.

Add bounded root-dictionary value replacement/insertion and value encoding,
preserving order, unknown bytes, original torrent `info` and edit-source buffer
lifetimes. Replacement values are independently validated before mutation. Keep
input/string/node/depth limits in force after edits, with rejection leaving the
document unchanged. Root-only editing avoids an unbounded retained chain of
overlapping backing buffers; nested state values can be edited as bounded
independent documents and replaced at the root.

Verify insert/replace, binary/nested values, copy/move lifetimes, invalid inputs,
depth/node/byte quotas, original-data identity and Java reads/hash preservation.
Both compilers and sanitizers are required. Operational resume trust, payload
flush, global cache invalidation and durable state write order are separate work.

Current GCC passes six native test entries, including insert/replace source
ownership and rejection quotas. An extended Java state check also passes: native
changes only `resume.data.valid` from1 to0 in a real generated operational state,
Java reads it and confirms unchanged torrent info hash, attributes and unknown
plugin fields. Original files stay untouched. Clang/sanitizer qualification is
next; this does not yet authorize operational profile/payload writes.
