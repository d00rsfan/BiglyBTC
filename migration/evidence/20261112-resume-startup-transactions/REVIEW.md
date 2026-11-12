# Startup resume transaction hardening and diagnostics

Scoped hardening tested. Recurrent real-library automatic Checking remains under
investigation; real-profile acceptance is pending. No next chapter started.

Native implementation 0942fd57146cd9421dda7e2ab17dcdd52655dc61; tree 99e312ba14b6b5e38b51efb9780f0d24e2ffe9ce. Original Java cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
RDResumeHandler.checkAllPieces/saveResumeData and
DownloadManagerStateImpl.getResumeData. SDK upstream
740a0b9aeabe00e762cc0efe4a0f27593db2550b; overlay parent
4ff478031095830fcfb7531dd6bc0f21b30906af; unchanged binary SHA-256
8aa740f2181b0295ca25154e680a623e7770832f19e37870add9f2d7d9a055b3.

## Change

Preparation persisted an all-RECHECK primary before restoring imported DONE bits
in a second save. Abandoning the intermediate state can invalidate checked pieces
without payload I/O. The old-primitive control reproduces this classification
loss; it does not prove the cause of the reported live-profile incident.

Managed preparation now stages edits and publishes its conservative map once,
before writable payload I/O. Explicit invalidation operations keep their existing
contract; crash backups still require verification. Known DONE pieces remain
DONE, and possible writes to missing wanted pieces remain conservative until
joined checkpoint. Historical maps are never promoted to trusted bits.

Local diagnostics correlate source identity with changed writes that can introduce
Checking: identity, reason, old/new validity, piece counts and publication result.
Healthy seed writes avoid reparsing old metadata for diagnostics. The development
launcher may mirror the output into its bounded owner-private log. Public Git
contains no real profile identities, paths, captures, URLs or console traces.

## Controls

Linux amd64, Qt6.12.0, original Java17, existing Release cache. Its directory name
is not evidence of sanitiser instrumentation. Build output stayed in local logs.

- `original-resume-transaction`: 24 cases and24 original Java BDecoder reads.
  Complete/partial and gzip/plain maps; abandoned staging; process termination
  after temporary sync, backup sync and primary rename; normal commit; old
  destructive-primitive control. Repeated with final log mirroring enabled.
- Affected managed V1 selected/invalid-flag, skipped Compact, and pure V2 restore
  with actual verified upload: 16 QTest passes.
- `general-chat-profile-publication`: pass.
- Existing ordinary IPv4 tracker/DHT/actual payload and cold/warm Host control:
  three QTest passes, about30s.
- Actual Host: 100 incomplete generated sparse torrents, 513 pieces of8MiB each;
  no traffic, zero invalid maps after joined close, about26s. No physical
  gigabytes allocated. Explicit scale diagnostic is not a default CI workload.

The affected restoration/Host/payload results preceded the final diagnostic-only
mirroring addition; the transaction test passed on the final linked code. No
persistence code changed in that addition. Exploratory tests that failed to
produce traffic were removed from CI and are not acceptance evidence.
Before hardening, 900-torrent direct-engine and900-torrent Host startup controls
also preserved their maps. Scale alone did not reproduce the real incident.
No full client/resume parity, sanitiser or real-profile acceptance is claimed.

## Next gate

Wait for the user to confirm Java Exit. Capture compact private resume sections,
identity digests and counts for the whole library without copying the profile or
reading payload. Then replace the development preview and let the user start it.
Compare baseline, source/write events and final maps. Already invalid sources
must not be mistaken for new SDK rejection or blindly restored from history.
Checking remains the priority over queue/CPU work and All Peers. No tools
interrupted Java or opened the real profile for Native in this verification.
