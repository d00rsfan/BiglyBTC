# Everyday torrent workflows — closeout scope draft

Source assessment only, 2026-10-24. This document does not declare the current
magnet boundary, Java handoff, secondary lanes, hosted run or portable preview
qualified. Root-owned execution receipts and final source hashes must establish
those outcomes before chapter closure.

## Fixed identities and coverage boundary

- Original BiglyBT: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
- Chapter native starting point: `14232b346e7dc70d45e8a41fee1746244a080b46`.
- Intermediate committed native core: `8795a76eef634bc6167713d913af7d302e3e028d`.
  The adopted magnet/Host work is subsequent working-tree source; it needs its
  own final full commit and raw source map.
- Reused libtorrent 2.0.12 source: `740a0b9aeabe00e762cc0efe4a0f27593db2550b`.
  Actual loaded/runtime artifact approval remains the existing separate gate.
- Scope authority: `migration/tasks/P03-07-everyday-torrent-workflows.md`, its
  five outcomes, and AD-001/AD-004/AD-005. Broad F01/F02/F05/F19/F25 and full
  Java engine/plugin/UI parity are not closed by this chapter.

All native paths below are relative to `preview/native/`. Upstream paths are
relative to the original repository at the full original commit above. Existing
capsule provenance retains exact SHA-256/blob identities and scoped line ranges;
it should be referenced rather than copied into another large audit.

| Production slice | Native subjects | Original source and covered behavior |
| --- | --- | --- |
| Saved Start/Stop/Pause/Resume/Queue/Force Start and ordering | `core/include/biglybtc/download_action.hpp`, `adapters/library_commands.{hpp,cpp}` | `core/src/com/biglybt/core/global/impl/GlobalManagerImpl.java`, `core/src/com/biglybt/core/global/GlobalManager.java`, `core/src/com/biglybt/core/download/impl/DownloadManagerImpl.java`, `core/src/com/biglybt/core/download/impl/DownloadManagerController.java`, and `uis/src/com/biglybt/ui/swt/views/utils/ManagerUtils.java`: saved state, pause/force separation and completion-qualified position pools. Exact subjects: `library-commands/source-pins.json`. |
| Shared My Torrents action admission | `ui/library_model.{hpp,cpp}`, `ui/library_window.{hpp,cpp}`, `app/session_bridge.{hpp,cpp}`, `app/main_desktop.cpp` | The same original action/controller behaviors above, mapped to one Host-authorized action mask and current hash/generation selection. Toolbar, Torrent/context and Transfers All share dispatch. This is not a source port of all SWT menus. `library-action-ui/manifest.json` and `all-dispatch-amendment/manifest.json` bind the focused change. |
| Multiple retained downloads and seeding | `engine/persistent_session.{hpp,cpp}`, `engine/managed_download.hpp` | Original DownloadManager/GlobalManager lifecycle intent, implemented through the reused LT session. Multiple registered handles, one pump, asynchronous control, retained seeding, exact retirement barriers and Close preserving saved intent. No full original queue/scheduler policy claim. |
| Compatible linear storage/checkpoint/completion | `engine/managed_storage.{hpp,cpp}` | `core/src/com/biglybt/core/disk/impl/resume/RDResumeHandler.java`, `disk/impl/DiskManagerImpl.java`, `torrent/impl/TOTorrentImpl.java`, `util/TorrentUtils.java`; original piece flags, recheck, incomplete path mappings and counters. Exact source identities: `managed-storage/source-pins.json`. Reuses qualified `profile.torrent_state` and `engine.multifile_linear` boundaries. |
| File selection/priorities | `adapters/file_policy_commands.{hpp,cpp}`, existing `core/file_policy`, `ui/file_model`, LibraryWindow Files actions, Host authorization | `GlobalManagerImpl.java` and `core/src/com/biglybt/core/disk/impl/DiskManagerUtil.java` compressed/legacy priority encoding. Host permits edits only without a retained handle; selected original indices and padding validation precede compatible state edits. Existing registry component `library.file_policy`; capsule `file-priority-ui/manifest.json`. |
| File intake and interrupted import recovery | `adapters/torrent_intake.{hpp,cpp}`, File/Open dialogs and Host | `GlobalManagerImpl.java`, `DownloadManagerStateImpl.java`, `TOTorrentImpl.java`, `TorrentUtils.java`: proper raw-info identity, original archive/state/inventory layout and opaque preservation. Native journal is bounded recovery machinery, not an original Java journal port. `torrent-intake/source-provenance.json` and `force-start-amendment/manifest.json`. |
| Explicit normal network policy | `engine/network_policy.{hpp,cpp}`, Host initialization | `ConfigurationDefaults.java`, `ConfigKeys.java`, `ConfigurationChecker.java`, `NetworkManager.java`, `ProxyLoginHandler.java`, `DownloadManagerStateImpl.java`, `AENetworkClassifier.java` plus scoped tracker/DHT/UPnP configuration subjects. `network-policy/source-provenance.json` contains full blobs/SHA/ranges and exact LT settings subjects. Recognized bind/proxy/private-torrent restrictions fail explicitly when no compatible route exists. |
| Persisted magnet intent | `engine/magnet_intake.{hpp,cpp}` | `core/src/com/biglybt/plugin/magnet/MagnetPlugin.java` (blob `61100228e3a9f763833f424250697e99f5dbeeb5`), `MagnetPluginMDDownloader.java` (blob `2cc5bf991e654c401f31014257ba095a39e0e18d`), `net/magneturi/impl/MagnetURIHandlerImpl.java` (blob `b5c03fde24f3aa191b9d56f5ab04bf78c4a0a3ec`), `UrlUtils.java` and `TorrentUtils.java`: original active-magnet map/args/hash/sources/timeout/start state. Exact supplementary configuration subjects: `magnet-intake/source-provenance.json`. Native destination/version are additive metadata, not an original field claim. |
| Actual metadata resolution and handoff | `engine/metadata_disk_router.{hpp,cpp}`, PersistentSession magnet branch, Host/main connection | Scoped metadata/hash semantics above, with actual LT metadata retrieval in the same owner, a registered non-filesystem metadata route, retirement before ordinary import, admitted numeric peer hints reapplied, and original pending removal after success. `metadata-disk-router/manifest.json`, `magnet-engine-tests/manifest.json`, `magnet-host/manifest.json` bind source. Engine/status errors never stand in for a successful metadata or payload observation. |

Current registry IDs describe larger inherited components:
`library.saved_inventory`, `profile.torrent_state`, `library.cached_view`,
`library.file_policy`, `engine.local_session`, `engine.multifile_linear`,
`engine.persistent_local_session`, `ui.classic_navigation_preview`. Append bounded
coverage/evidence under the appropriate components, or introduce a specifically
bounded new component. Do not fill broad `first_ported_commit` or
`integrated_through_commit` with this chapter's hash for entire original classes.

## Implemented user path to qualify

Open a chosen exclusive profile; add a .torrent or magnet with destination; view
stable rows; Start/Stop/Pause/Resume/Force Start/Queue selected downloads; order
within qualified completion pools; stop before changing nonpadding file priorities;
retain completed handles for seeding; close without rewriting the user's saved
intent to Stop; reopen against compatible original files. Persisted magnets resolve
and become ordinary rows under the same original hash. Interrupted import recovery
must preserve a later user's Stop/force change. GUI callbacks remain value-owned,
worker-only IO and stale generation checked.

## Material remaining workflow/qualification limits

1. **Legacy magnet destination recovery needs the new amendment qualified.**
   `magnet-legacy-destination/manifest.json` freezes a five-file follow-up:
   re-submit the exact original pending URI with an explicit destination, exact
   raw-CAS attach, preserve original intent/unknowns, reject conflicting URI,
   changed destination or retained handle, and retry safely. Until adopted and
   tested, the preceding native source only retains/cancels those legacy rows.
   No automatic path inference or silent relocation is provided.
2. **Intake options are deliberately small.** File intake asks metadata,
   destination and Start; file selection is available after import while stopped.
   Original pre-add per-file/options dialogs, active priority changes, removal vs
   deletion, moving existing data, torrent creation and OS associations are not
   supplied by these controls. Removal/deletion and broader shell integration were
   outside this declared chapter; initial selection timing should be stated.
3. **Queue/network compatibility is bounded.** These actions preserve explicit
   original intent and qualified order; they do not port every original scheduling
   or seeding rule. Tor/I2P, alternate hybrid identities and unsupported storage or
   policy combinations fail explicitly. A settings translation is not evidence
   for every uTP, Mainline DHT, PEX, web-seed, IPv6, proxy/private-torrent or
   interface-change path. Final evidence must name the protocols actually tested.
4. **Handoff and seeding require actual receipts.** `managed-handoff/` stages real
   partial then complete Java→native→Java v1 single/multifile reload checks;
   `managed-seeding/` stages actual post-completion uploads and selected-completion
   recovery. Source authoring alone establishes none of those runtime outcomes,
   nor v2/hybrid/full malformed-profile or crash-matrix coverage.
5. **Publication is separate.** Final connected compiler/sanitizer/hosted checks,
   exact loaded source runtime, private packaged worker tests, portable promotion
   and complete rollback/source maps remain root-owned closeout gates. Earlier
   Statistics dist and the intermediate core checkpoint are not the final chapter
   artifact. No original running client, ordinary profile or payload was used by
   source authoring; public-swarm testing is not a substitute for owned fixtures.
