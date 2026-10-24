# CF-06 qualified slice and compatibility boundary

The chapter implements one selected ordinary torrent at a time: remove its
library entry while retaining data, explicitly delete its listed data leaves,
move its save parent, or point it at existing data under a new save parent.
Existing metadata archives remain. Destination directories must already exist;
relocation never merges an existing torrent root or overwrites a leaf. Empty
source directories and unrelated files remain. Original do-not-delete flag0x40
is authoritative. Preparation only reads; explicit confirmation precedes engine
retirement, fresh scope validation and durable intent publication.

All destination leaves precede saved-path publication; original source leaves
remain until publication succeeds. Same-filesystem relocation uses hard links,
and cross-device relocation uses exclusive copy/sync/content checks. Listed
partial/incomplete-name files are supported within the authenticated linear
mapping. Relink changes no payload and invalidates saved verification. The Host
retires/certifies/forgets the old storage handle, preserves startup/force/pause
intent, and optionally readmits the same torrent against the new mapping. A
Scheduler-owned pause transfers only acknowledged exact admission authority;
unknown or inherited pause authority never acquires automatic resume rights.

Recovery uses a private identity-bound confirmed-intent journal before owner
admission. It compares exact affected state and sparse target inventory/cache
rows, preserving unrelated row checkpoints. Changed target documents, unknown
scratch files and torn unpublished journal writes require explicit review.
Cancellation is effective before confirmed intent is published; journal replay
settles that already-confirmed operation. Uncooperative same-UID concurrent file
replacement cannot be excluded atomically by POSIX path checks; the caller holds
the profile lease and must exclude other payload writers.

Creation supports standalone ordinary v1 file/directory sources, tracker tiers,
private/comment settings and Auto plus twenty piece sizes32KiB–128MiB. Public
LT2.0.12 generates metadata; descriptor-based hashing retains source identity.
Source contents must remain stable during hashing. There is no filesystem
snapshot guarantee. Output never overwrites, and post-publication sync failure
is reported as an already-published output. Optional library import is explicit,
defaults off, and adds stopped. No hidden import, payload write or tracker call.

Remaining original-family work is explicit: bulk workflows; original wizard
families beyond ordinary file/directory v1 (including256MiB manual pieces,
v2/hybrid, virtual/multi-source/advanced options); archive deletion controls;
external links, nonlinear compact/reordered mappings, overlapping/shared paths
and merge/overwrite moves; broader interrupted-operation repair UX. Empty-only
sources are rejected consistently with the original creator. CF-01/CF-02 own
wider storage/identity qualification; this slice does not close those families.
Offscreen checks do not establish compositor/public-swarm/performance parity.

Current file-operation identity/recovery validation reads payload contents for
non-Keep operations. Large moves, relinks and deletions can therefore incur
substantial extra IO, including same-filesystem hardlink moves. This chapter
qualifies correctness; it does not establish a performance improvement. CF-08
must profile and remove avoidable full-payload reads while retaining explicit
approval, inode/path ownership and crash-recovery guarantees.
