# Selected-v2 hybrid warm resume

Native `55e18aa6addb8bd829273727471c4339144280b7`, tree `09430e0c5b9cd7c7df2f1d3a7dc9870ec65b3f08`, removes the remaining explicit hybrid-v2 warm-resume limitation in normal
existing-library admission. It uses the already authenticated original/SDK
geometry and selected-swarm bridge, authenticates all required Merkle piece
layers against info-bound file roots and applies the same saved DONE-prefix,
identity, length, timestamp, storage-layout and recovery proofs as pure v2.
Missing/corrupt proof is deferred before persistent mutation; no guessed checked
map, automatic whole-torrent recheck or restored counter totals were added.
The SDK binary is unchanged: SHA256 `8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3`.

## Immutable original reference

BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
core/src/com/biglybt/core/disk/impl/resume/RDResumeHandler.java,
core/src/com/biglybt/core/torrent/impl/TOTorrentImpl.java and
core/src/com/biglybt/core/torrent/impl/TOTorrentDeserialiseImpl.java. The native
work builds on the preceding immutable snapshot/owner-lifetime work, with no
upstream source or SDK modifications.

## Qualification

19 runtime QTests cover existing pure-v2 behaviour and hybrid-v2 16/32/64 KiB
pieces, aligned and short files, declared virtual padding, root-only short-file
proof, two warm launches with actual selected-v2 upload, and partial resume
with full physical size or a short missing tail. Partial `valid=0` and `valid=1`
maps close without traffic, reopen, then finish through an actual v2-only
loopback seeder. Initial warm owners report zero storage hash operations.
Checkpoint/reopen preserves raw info, opaque fields, known piece maps and data;
virtual pads never become disk files. Newer payloads, absent/corrupt layers and
missing/truncated DONE files refuse safely, preserving state and inventory.
All 16 affected merge QTests passed, including the pre-existing hybrid-v2 cold
path, skewed geometry, cancellation and retirement controls.

The source-built original JDK17 core read nine actual new Native checkpoints:
six complete and three partial. TOTorrentFactory/TOTorrentImpl retained both
full hashes and selected the v2 prefix; original piece counts matched; the
actual RDResumeHandler completeness predicate accepted their meaning. The
helper invokes original APIs rather than copying their rules. Its fence blocks
network/listener/child-process activity and confines writes to the generated
oracle root. No Core/client was started. Standalone deserialisation logged
missing dht URL-handler notices because client bootstrap was absent; API checks
still passed. This is not a new tracker/DHT defect or full Java runtime oracle.

Tests used generated profiles, small generated data and owned loopback peers.
No real profile/payload was accessed, no running process was interrupted and
no temporary build image was archived. Qualification is Release; no additional
sanitizer matrix or independent human/sub-agent review is claimed.

## Reproducing the original reader

Build the native managed-download tests with the existing qualified SDK. Set
BIGLYBTC_TEST_HYBRID_RESUME_EXPORT to a fresh generated oracle/checkpoints
folder and run pureV2SavedDoneMapRestoresAndServesVerifiedBytes plus
selectedV2HybridPartialWarmResumeDownloadsOnlyMissingTail. Compile the two
HybridResume*.java files in preview/native/tests/reference against the pinned
original core classes using JDK17. Create generated-fixtures.marker in the
oracle root. Run HybridResumeReaderOracle with java.security.manager set to
HybridResumeFence, biglybtc.test.root set to that root and biglybtc.test.classes
to the original classes. Set user.home, java.io.tmpdir and azureus.config.path
inside the generated root; clear JAVA_TOOL_OPTIONS/JDK_JAVA_OPTIONS/_JAVA_OPTIONS
and CLASSPATH. Verification records the exact original class and raw log hashes.

## Delivery and next work

The user is observing the preceding frozen preview; it remains unchanged.
Promote this candidate and compare private baseline/state only after normal
Exit. Whole F05/F18/P11 and real-profile interoperability remain open. Next:
remaining advanced-engine/lightweight seeding and native UI dependencies,
keeping RSS/embedded tracker/I2P/IP-filter low and browser/player last.
