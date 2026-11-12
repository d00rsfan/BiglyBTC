# Existing-library startup: resume transaction and diagnosis

The recurrent automatic Checking report remains under investigation. Do not
start All Peers or claim acceptance of the real library. Public evidence uses
generated data only; all real-profile identities, paths, state captures and
console logs remain private and outside Git.

A normal preparation used to persist an all-RECHECK primary before assembling
and saving the imported map. Abandonment or failure between these saves could
leave checked pieces invalidated without payload I/O. Stage the metadata edit,
assemble the conservative map, then publish it once before allowing payload I/O.
Keep the existing durable invalidation primitive for operations that require it.
Never restore historical DONE bits automatically or suppress legitimate rechecks.

Record local resume writes capable of introducing Checking, including the full
identity, reason, old/new validity and piece-state counts, and publication result.
Correlate with the source-read identity. Mirror development diagnostics only when
explicitly requested by the launcher. Avoid reparsing old metadata for healthy
seed writes. No profile backup or permanent JVM is added.

Verify abandonment, process termination at each save stage, compressed/plain
state and complete/incomplete maps, using the original Java BDecoder reader.
Run affected V1, pure V2 and Compact restoration plus one actual ordinary Public
tracker/DHT/peer transfer and warm reopen. A generated sparse scale diagnostic
is explicit only, never a long default CI workload.

Original Java baseline: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, especially
RDResumeHandler.checkAllPieces/saveResumeData and DownloadManagerStateImpl.
SDK upstream: 740a0b9aeabe00e762cc0efe4a0f27593db2550b; existing overlay parent
4ff478031095830fcfb7531dd6bc0f21b30906af. No SDK change.

Next acceptance step, agreed with the user: prepare the diagnostic image first;
wait for confirmation of Java Exit; capture compact maps for the whole inventory
without reading payload or writing the profile; then ask the user to start the
preview. Compare the baseline with source/write events and final primary maps.
No real-profile launch is performed while Java owns the profile.
