# Library lifecycle performance follow-up

Native a17d7b0fa7e0a50851a977770682ab14452ad9a7, tree c5b55e4d958673201b13eb4dccee82d859c14e99; original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. The bounded startup/shutdown fixes and
requested replacement test build are complete. Stop after delivery; no next
feature chapter started. Whole-client, Java performance and real-profile
acceptance of this candidate remain incomplete.

## Changes and source relationship

The original GlobalManagerImpl stop/save lifecycle builds and saves one global
inventory separately from per-download stopping. Native formerly rebuilt and
persisted the entire downloads.config for each retiring owner. Eligible normal
Public close now stages at most 32 rows against one immutable inventory/index.
Each changed per-torrent state is still saved by the existing durable writer;
the global inventory is then published once through its unchanged source CAS,
backup, temporary-file fsync, rename and directory-sync protocol. Unknown root/
row fields, pause data and current/legacy layouts are preserved. No new profile
format, persistent batch journal or backup mechanism is introduced.

Only the trusted Host opts into BatchPublic after non-Public source controllers
have retired. Default/custom engine closes remain individual unless explicitly
opted in. Pending completion renames/journals, allocation and manual/selected
verification use the existing individual path, flushing any staged inventory
first. Actual SDK flush/removal/weak-expiry and storage-release/join fences are
unchanged. No checkpoint success escapes the owner call before inventory commit.
A refused/failed batch marks every staged participant failed and clears its
history-checkpoint authority; a destructor never publishes or retries. Individual
state and inventory are still separate durable publications, as before; an
interrupted batch can leave up to 32 state updates before inventory publication.
The existing failure warning remains required for an uncertified normal close.

The completed-file path also performed a second state backup/save even when
final filelinks2 exactly matched the already persisted attributes. writeLinks
now reports a real attribute change, and that second save is skipped only for
an exact no-op. Actual suffix/prefix/link changes and completion-journal ordering
retain their original writes. No fsync was removed from a changed publication.
Aggregate mode/publication/row counts are added to shutdown diagnostics without
names, paths, hashes or tracker URLs. The 1000-second ceiling is unchanged.

Original RelatedContentManager.isEnabled reads its configuration state independently
of loading the result cache. Native's frequent chat-network source gate called
SwarmDiscoveryProfile.searchPermission, which also opened and decoded the complete
related-content catalogue/layout. A local CPU sample identified this call path.
Permission reads now refresh config/settings alone. The config memo is published
only after validation; missing/malformed/recovery settings do not return stale
permission. Full catalogue, source/library, lane and privacy checks remain in
actual data-producing consumers. The existing per-owner admission selector's
allocation behaviour is preserved outside shutdown batching.

## Final qualification

GCC16/C++23/Qt6.12 Release desktop application links; external SDK is unchanged.
No sanitizer matrix or full SWT lifecycle equality is claimed for this follow-up.

- 500 permission checks observed with Linux inotify do not open catalogue,
 deletion, table-layout or inventory files. Config disable is observed immediately;
 repeated malformed config refuses permission; full refresh still refuses the
 malformed optional files. Existing original codec/cache/profile tests pass15.58s.
- The compact/pure-v2/hybrid/partial resume suite reports26 passes (including setup
 and cleanup) in35.022s, zero skips,
 including interrupted/unclassified maps and throwing/reentrant close observers.
- 61 managed-storage families /4776 assertions pass3.78s, including real completion
 suffix/link/journal paths and recovery. Inventory transaction QTests pass0.16s,
 covering current/legacy merged results, exact prior backup, source CAS refusal,
 pre-existing saving candidate, duplicate targets and no destructor publication.
- 64 real managed owners close cleanly in1601ms with3 inventory publications;
 maps and final counter/storage receipts remain valid.
- 64 owners with an intentionally late inventory edit reject14 staged participants,
 retain the external unknown field, join/release storage and preserve all maps.
 The expected checkpoint failure is reported rather than converted to success.
- 512 generated persistent Host torrents (2 pieces,2048 opaque inventory bytes/row)
 close in15051ms with35 inventory publications; all512 maps stay clean before and
 after close, with joined/released storage and no checkpoint/accounting error.

The retained historical measurement for the same generated512/piece/padding
parameters was31784ms at native f0599e687ba9b1b8c4db9d0cafe6ce473cb1acba.
This is a historical comparison on a shared machine, not a controlled Java A/B
measurement or a prediction for another library. An intermediate draft exposed
the remaining duplicate state save; the final tests above include its removal.
The new inventory test fixture initially omitted the original required saved
state field; the fixture was corrected without relaxing production validation.
No unrelated long green network suite was repeated.

[Qualification](qualification.json), [selected source scope](source-scope.json)
and [frozen build receipt](build.json) retain finite generated evidence. All
profiles/payloads in these checks are generated, DHT/Internet/tracker use is disabled
for the scale runs, and temporary fixture roots are removed automatically. No
user profile, torrent/chat identifiers, runtime traces or hardware details are
included here. Live diagnostics remain private; the raw CPU capture was deleted.

## Delivery and limits

The stable chat-preview/daily-use launcher selects this source-qualified image
on the next user launch. Frozen executable/provider hashes and launcher syntax
are checked without launching the application or opening the live profile.
First launch after normal Java Exit may capture one compact private service-state
baseline; no full profile copy or payload scan is performed. The prior image can
be removed only after its manifest lock is released and its exact hash matches.

Actual real-library startup/Exit timings and Java switching remain user acceptance.
Other legitimate result-catalogue operations, individual state durability costs,
non-Public checkpoint batching and broader performance parity are not covered by
these fixes. Do not claim whole migration completion or advance global integrated
upstream baselines. Stop here after the requested build.
