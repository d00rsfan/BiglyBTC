# Swarm file merge: bounded native chapter

The current preview includes verified local reuse between matching files, with
compatible original settings, buffered `mergedata`, live Merged Data columns and
Statistics → Swarm Merge. It also includes the already qualified resize,
15 core torrent property views and bounded graceful-close work. The user asked
to stop after this chapter; no subsequent chapter has started.

## Immutable provenance

Original BiglyBT: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Sources reviewed:
`core/src/com/biglybt/core/global/impl/GlobalManagerFileMerger.java`,
`core/src/com/biglybt/core/config/impl/ConfigurationDefaults.java`,
`core/src/com/biglybt/core/download/DownloadManagerState.java`, and
`core/src/com/biglybt/core/disk/impl/DiskManagerRecheckScheduler.java`.
Native core implementation and fast-resume gate fix:
`4944cb303de545980412dd5a260bb31ce88facc8`. Selected-v2 target proof refinement:
`d60d3b1e271f24fdd49b18c387d694813164b8bd`; final source tree `54b2a509ca4903bbadd24a407bea1742c9870fd0`.
The existing source-qualified libtorrent2.0.12 provider is unchanged;
SHA256 `8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3`. No stock-provider equivalence is claimed.

## Behaviour

Import the exact original keys: Merge Same Size Files=true, Extended=false,
Tolerance=0, Min Pieces=5. Extended is required to use completed owners.
Exclude skipped/padding/low-noise/metadata files. Prefer matching Merkle roots;
then use original same-size and same-name tolerance groups. A second pass admits
small files only after their owners have a qualifying large-file relationship.
Similarity selects candidates and never proves content or enables discovery.

Use the retained engine actor and its sole alert consumer. A single independent
read/hash worker owns authenticated, read-only storage snapshots. It uses source
HAVE/finished blocks and actual target boundary bytes, including different file
and piece offsets. Only declared virtual padding is zero-filled; absent real
bytes refuse the attempt. Verify the entire target piece before default
non-overwriting SDK add_piece; use SHA1 for selected v1 and authenticated v2
Merkle layer/root proofs for selected v2, including hybrid-v2. Revalidate exact
admissions, running state and binding before submission. Three hash failures
back off until actual source/target progress changes.

Stop/Pause/disable cancel unsent work. Readers and their descriptors end before
storage retirement; close joins the read worker before checkpoints/profile
release. Already-submitted SDK writes retain the existing flush/removal/join
fence. Local scheduled bytes use original `mergedata` (distinct from column ID
`mergeddata`), excluding already peer-finished/writing blocks observed at
submission. Engine-confirmed pieces are reported separately. Like Java's
writeBlock counter, this is scheduled local supply, not a claim about exact
physical writes after later peer races. Network/global payload counters remain
network-only. Opaque state and original counter prefixes survive checkpoints.

A focused restored-v2 case exposed an obsolete disk-wait flag after fast-resume
validation had already completed. Clear that flag on actual checked state,
retaining queue, manual pause, verification and stop authority. No automatic
full recheck or synthetic completion was introduced.

## Qualification

-14 QTests with generated normal-library profiles: v1, purev2 and selected-v1
 hybrid reuse, complementary partials, mismatched candidates, skewed boundaries,
 tails/padding, Extended/disable/Pause, pending read retirement and actual recheck,
 pathname replacement, cancellation, compatible checkpoint/reopen, exact local
 supplied totals, zero peer/global payload and no repeated credit on reopen.
-4 focused QTests additionally cover selected-v2 hybrid reuse and geometry
 through its supported cold-start path (two test cases plus fixture lifecycle).
-4 merge UI QTests,92 Statistics QTests,16 properties/resize/passive-table QTests.
-10 focused resume/Pause QTests,13 settings families/522 assertions, pure planner
 defaults/eligibility/root/tolerance/two-pass/bounds checks.

Generated data and loopback endpoints only; no real profile was opened or
written, no real payload was read, and no Java process was interrupted. This is
native runtime acceptance plus source review, not a Java runtime differential
merge oracle. Raw logs remain local; hashes and aggregate results are recorded
in verification.json. [Synthetic UI capture](swarm-merge.png) renders generated
presentation packets; runtime data flow is qualified by the separate tests.

## Limits and remaining work

Runtime bounds:200000 indexed files/16MiB names,64 members per group,4096
candidate pairs,32 pending operations/64MiB buffer reservation,16MiB target
pieces,128 assembly spans/read fragments, bounded SDK block/piece snapshots and
256 displayed pairs. Oversized/unsupported geometry or absent authenticated
v2 layers is reported as unavailable; existing data is not guessed or repaired.
The merger adds no tracker/DHT/publication permission or public identity data.

Saved selected-v2 **hybrid warm resume** remains explicitly unsupported by the
existing admission layer; the new cold-path tests do not authorise bypassing
that guard in an ordinary library. Broader advanced engine/Java UI parity,
large-library startup/steady CPU and real-profile round-trip acceptance remain
open. Whole F18 also includes lightweight seeding and stays in progress.
No embedded browser/player or unrelated IP-filter work was started.

## Delivery and stop boundary

The same preview image was replaced under its exclusive runtime lock with
SHA256 `552804ad2855904f5a504b7459d7d0de3eda4d4abbda736195ae73e3119e2401` (19825592 bytes). No rollback image or automatic application
launch. The stable chat-preview launcher is retained. A normal Java Exit and
fresh compact metadata baseline are required before the next diagnostic Native
launch. Stop here; suggested next chapter is large-library startup and sustained
seeding diagnostics, subject to the user's next instruction.


Subsequent qualification: the selected-v2 hybrid warm-resume limitation above
is resolved by native `55e18aa6addb8bd829273727471c4339144280b7`; see the [later review](../20261113-selected-v2-warm-resume/REVIEW.md). Historical cold-path results and broader F18 gaps are unchanged.
