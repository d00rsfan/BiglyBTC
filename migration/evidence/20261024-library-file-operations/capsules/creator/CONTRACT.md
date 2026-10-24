# CF-06 v1 torrent creation

Source-only staging against native `a965fd3cf0547e041d58512c56fea1cccf972ccb`
and original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. No compiler,
subject program, Java, network, session, live profile or native-tree mutation
was used to author this capsule. Root owns adoption and qualification.

`engine::createTorrent` is a synchronous worker operation. Options are copied
before invoking any callback. It creates metadata only. Progress carries stage,
actual hashed bytes, discovered file count and completed/total pieces; initial
Scanning has unknown totals represented by zero. Cancellation and callback
exceptions abort before publication. No callback executes after the commit point.
The caller marshals notifications to the UI; callbacks do not own widgets.

The source is one absolute regular file or directory. Every path component is
opened relative to an authenticated directory descriptor with `O_NOFOLLOW`;
nonregular entries, invalid UTF-8 names, backslashes and dot aliases fail. Input
is read-only. Files are enumerated in deterministic unsigned UTF-8 byte order,
recursively per directory, including hidden files and mixed empty files. No
original global ignore list, BYO linkage map or optional ED2K/per-file digest is
silently applied. Empty directories add no torrent file. A zero-total source is
rejected, matching original creator `RT_ZERO_LENGTH` and LT's generator.

Bounds: 8192 files, 2048 subdirectories, depth64, 4096 relative-path bytes,
16 TiB total plus LT's smaller per-file bound, 1,048,576 pieces, 32 MiB encoded
metadata, 16 nonempty tracker tiers, 64 URLs, 2048 bytes per URL and 16 KiB
comment. UTF-8/comment and supported piece-size validation precede payload reads.
Only http/https/udp tracker strings are accepted; no URL is contacted. Duplicate
trackers follow LT's existing de-duplication, keeping the first tier.

v1 generation uses exact LT2.0.12 `file_storage`/`create_torrent::v1_only`, public
SHA1 `hasher` and `set_hash`. Public `set_piece_hashes(path)` reopens files and is
therefore not used. Pieces concatenate actual files in their recorded order,
including cross-file boundaries. No synthetic padding/mtime attributes. Auto
piece size uses the original floor(total/size)<=2048 rule at powers of two from
32 KiB to4 MiB (the original lower-piece-count parameter is unused). Manual
choices reproduce the original standard list through128 MiB; LT rejects the
original final256 MiB choice, so it is unavailable. v2/hybrid creation is outside
this v1 workflow and must not appear as a working control.

Original wizard defaults: computed pieces and v1; comments from
`CreateTorrent.default.comment`; source/save folders from
`CreateTorrent.default.open/save`, with save fallback
`General_sDefaultTorrent_Directory`. The suggested output is source+`.torrent`,
with its basename moved to the configured save directory when present. Private
uses `CreateTorrent.default.privatetorrent` (fallbackfalse); autoOpen/autoHost
start false. Those UI/config choices belong to Host/UI, not this engine helper.
Unlike original overwrite confirmation, this helper never replaces an output.
Unlike original creation, creation-date is omitted and creator is `BiglyBTC`,
so repeated stable inputs/options produce identical bytes. No original creator
whole-byte equality is claimed. Java UTF-16 ordering can differ from our UTF-8
ordering for non-BMP names; both describe their actual recorded file order.

All source files are authenticated before/after streaming. After metadata is
written and fsynced, the complete source tree is re-opened and compared for
root/directory/file identity, mode, length, mtime and ctime, including added or
removed entries. Output ancestry cannot be inside the source directory. The
current source/output parents are re-authenticated before publication. These
checks detect observed changes; they are not a filesystem snapshot or a lock
against another writer. Caller must keep explicit source/output owned and
stable for the operation. No payload permissions/content/timestamps are written;
ordinary read atime behavior is controlled by the filesystem.

A random0700 private temporary directory and0600 metadata file protect staging
from other users sharing the output parent. `linkat` publishes with atomic
no-replace semantics. Existing regular files, directories and symlinks all
remain untouched on conflict, including a name created after hashing. File
fsync occurs before link; temporary names are removed and the parent fsynced.
Before link any error is `unexpected<CreateError>`. After link, directory-sync
or cleanup failures return a result with `PublishedDirectorySyncFailed` or
`PublishedCleanupFailed` and the original system error. Such a result is already
published: do not automatically retry or report that no output was created.
A crash can leave private staging debris, but cannot overwrite an existing final
name; no sweeping unrelated directories is performed.

Qualification source:13 meaningful plain regression families plus an optional
thin native-driver -> actual unchanged original Java decoder handoff. The latter
compiles only two authored helpers, checks retained5432-class hashes before/after,
uses existing ReferenceFence/peer_interop.jvm and generated `/tmp/biglybtc-*`
fixtures, then compares actual Java hashes, piece bytes, file order/length,
Unicode, trackers, private/comment and simple-vs-multi interpretation. It never
starts the original core or contacts trackers. All execution outcomes remain
pending root's consolidated batch.
