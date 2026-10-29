# Migration Feature Coverage

## How to Use This Matrix

This is the initial scope inventory, not a declaration of complete discovery. The family rows remain **partial and unverified**; bounded implementation rows
are listed below with their evidence. P00-02 must expand each family into independently testable behavior rows, including configuration options, protocol variants, UI actions, platform differences, and error paths. Newly discovered behavior gets a row before implementation or an explicit scope decision; it cannot be discarded because it was absent from this seed list.

For each expanded row, record: stable ID, source repository/commit/paths, desired behavior, native component/target, task/owner, dependencies, fixture/test IDs, implementation commits, status, and evidence. Status is planned, implemented, verified, deferred, or retired. A decision reference is mandatory for deferred/retired rows. The corresponding component baseline belongs in [the source lock](upstreams.lock.json), not in a disconnected spreadsheet.

In source pointers below, `core/` means `core/src/com/biglybt/core/`, `plugin/` means `core/src/com/biglybt/plugin/`, and `UI/` means `uis/src/com/biglybt/ui/`. These are discovery entry points, not exhaustive ownership boundaries.

## Main Application Families

| ID | Behavior family | Source entry points | Main phase | Minimum acceptance focus |
| --- | --- | --- | --- | --- |
| F01 | Torrent parsing, creation, versions, magnet/metadata intake | `core/torrent`, `core/torrentdownloader`, `plugin/magnet` | P03/P05 | Exact hashes/encodings; malformed metadata; supported-version matrix |
| F02 | Storage, priorities, verification, allocation and file movement | `core/disk`, `core/diskmanager` | P03 | Corruption, partial data, large files, disk-full, safe paths and relocation |
| F03 | Peer wire protocols, piece selection, transfers and seeding | `core/peer`, `core/peermanager`, `core/networkmanager` | P03/P05 | Controlled interoperability, flow control, cancellation, private-torrent rules |
| F04 | Trackers, Mainline DHT, metadata discovery, PEX and web seeds | `core/tracker/client`, `plugin/tracker`, `plugin/extseed`; mlDHT sources | P05 | Network/extension fixtures, retries, IPv4/IPv6, discovery isolation |
| F05 | Session lifecycle, resume, completion and restart recovery | `core/download`, `core/global` | P03/P11 | Crash consistency; imported resume validation; no data loss |
| F06 | Bind/proxy/DNS policy, network selection and IP filtering | `core/networkmanager`, `core/proxy`, `core/ipfilter`, `core/ipchecker` | P05/P07 | Interface changes; no forbidden direct fallback or cross-network leak |
| F07 | Global/per-download limits, automatic speed management, scheduling | `core/speedmanager`, download/global policies; SpeedScheduler | P04/P08 | Units, policy precedence, overrides, clock changes, ownership of pauses |
| F08 | NAT traversal, router mappings and firewall behavior | `core/nat`, `plugin/upnp`, `core/src/com/biglybt/net` | P05 | Fake-router leases, failures, IPv6 behavior, cleanup; no real router mutation by default |
| F09 | BiglyBT/Azureus DHT routing, transport, persistence and network separation | `core/dht`, `plugin/dht` | P06/P07 | Distinct from Mainline; wire interoperability and bounded routing state |
| F10 | Distributed database and transfer semantics | `core/src/com/biglybt/pifimpl/local/ddb` | P06 | Keys/values, timeouts, transfers, quotas and malformed input |
| F11 | Buddy/chat/message identities, synchronization and notifications | `plugin/net/buddy`, `core/crypto`, `core/security`; azmsgsync | P06/P07 | Signatures, persistence, ordering, replay/duplicate handling and cancellation |
| F12 | Related content, lookup and rating integration | `core/content`; aercm/azrating | P06/P08 | Distributed results and UI/API behavior, not only presentation |
| F13 | Categories, tags, constraints and rule interactions | `core/category`, `core/tag` | P04/P08 | Deterministic event traces, user overrides and persisted rules |
| F14 | History, archive, backup and restoration | `core/history`, `core/backup`, download manager | P08/P11 | Retention, round trips, collision handling and recoverability |
| F15 | Start/stop, queue, seeding, removal and automation policy | `plugin/startstoprules`, `plugin/removerules`, `core/global` | P08 | Queue fairness, ratio rules, force-start and explicit deletion semantics |
| F16 | Metasearch, subscriptions and content acquisition | `core/metasearch`, `core/subs`, `core/content` | P08/P10 | Provider contracts, scheduling, duplicate handling, failures and cancellation |
| F17 | Sharing, RSS and embedded tracker server | `plugin/sharing`, `plugin/rssgen`, `core/tracker/server` | P08 | Publication/access policy and local client interoperability |
| F18 | Advanced engine features including merging and lightweight seeding | `core/global/impl/GlobalManagerFileMerger.java`, `core/lws`, peer controls | P05/P08 | Explicit gap map against selected engine; cross-file integrity and limits |
| F19 | Main window, library tables, intake, actions and menus | `UI/swt/views`, `UI/swt/shells`, `UI/swt/TorrentMenuFancy.java` | P04 | Selection, columns, cascades/tabbed popup, focus, stale objects and detached views |
| F20 | Peer/piece/tracker views, graphs and transfer statistics | `UI/swt/views`, `UI/swt/components/graphics`, `core/stats` | P04/P08 | Correct values/units, hit testing, bounded history, hidden-view paint suspension |
| F21 | Settings UI, keyboard access, accessibility, layouts and themes | `UI/swt`, `core/src/com/biglybt/ui/config` | P04/P11 | Setting coverage, navigation, validation, high-DPI/RTL/platform behavior |
| F22 | Remote HTTP/RPC, pairing, secure tunnels and web assets | `core/pairing`, `core/src/com/biglybt/ui/webplugin`; xmwebui | P10 | Auth/session/read-only/error contracts; legacy client compatibility |
| F23 | Headless, console, command-line and legacy remote interfaces | `UI/none`, `UI/console`, `UI/telnet`, `core/src/com/biglybt/pifimpl/remote` | P03/P10 | Scriptable operations; explicit disposition for unsafe/obsolete exposure |
| F24 | Playback, transcoding, media streaming and devices | `core/devices`, `core/content`; media plugins | P09 | Seek, incomplete data, HTTP ranges, helper failure and device behavior |
| F25 | Configuration, profiles, import/export and persisted identities | `core/config`, `core/vuzefile`, plugin settings | P02/P11 | Dry-run import, schema migration, secrets, isolation and preserved originals |
| F26 | Platform integrations, startup, tray, sleep and instance control | `core/src/com/biglybt/platform`, `core/instancemanager`, `core/drivedetector`, `assets` | P04/P11 | OS-specific lifecycle, associations, permissions, recovery and cleanup |
| F27 | Installation, update checking, native updater and rollback | `core/update`, `core/versioncheck`, `core/src/com/biglybt/update`; azupdater | P02/P11 | Trusted artifacts, interruption, locked files, rollback and safe restart |
| F28 | Plugin/service contracts, lifecycle, IPC and extension points | `core/src/com/biglybt/pif`, `core/src/com/biglybt/pifimpl`, UI extension APIs | P02/P08 | Capability/version rules, ownership and no false Java-JAR compatibility |
| F29 | Diagnostics, logs, tests and optional network measurements | `core/logging`, `core/stats`, `plugin/net/netstatus`, `plugin/net/test`; mlab | P02/P10 | Bounded/redacted logs, explicit measurement initiation, error visibility |
| F30 | Cross-cutting security and resource safety | `core/security`, `core/crypto`, parsers/network/file/update boundaries | P02–P12 | Fuzzing, authentication, safe paths, secret handling and bounded allocations |
| F31 | Localization, assets, customization and compatibility notices | `core/internat`, `core/custom`, `UI/images`, `UI/skin`, `assets` | P04/P11 | Locale fallback, text expansion, asset provenance and accepted visual differences |
| F32 | Remaining integrations and feature-discovery closure | `core/messenger`, helpers/utilities, build resources and source census | P00/P12 | Every discovered behavior maps to a tested feature or accepted disposition |

F32 is an inventory safeguard, not permission to place unknown features into an untested miscellaneous bucket. Expand it into named rows during discovery.

## Initial bounded behavior rows

Bounded slices include locally committed implementations and scoped review/test
evidence. Full native hashes are recorded in [the component registry](upstreams.lock.json),
[header provenance](evidence/P06-08-azureus-dht-headers/provenance.json),
[owned STORE committed inputs](evidence/P06-24-owned-store/committed-inputs.json),
and [Tor ownership committed inputs](evidence/P07-01c-tor-ownership/committed-inputs.json).
Family/platform and release coverage remain open. The full Java baseline and
paths stay in each matching source-lock component; local records do not advance
its integration baseline or declare the family verified.

| ID | Behavior and native component | Task | Status and evidence |
| --- | --- | --- | --- |
| F19.actual_library_shell | Real saved library in a Qt6.12 native window | P04-01-native-library | In progress; owned snapshots and stable targets |
| F19.local_continue_command | Continue/cancel a generated local workload through shared native services | P04-01-native-library | In progress; no public activation |
| F30.ui_worker_ownership | Profile lease and file/network work stay on one bounded owned worker | P04-01-native-library | In progress; queued value results and teardown checks pending |
| F19.saved_library_view | Owned actual inventory/cached metadata for the native desktop | P04-01-native-library | In progress; read-only core/cache/profile view implemented, qualification next |
| F05.cached_metadata_view | Original optional gzip cache names, sizes, type and display overrides without per-state scans | P04-01-native-library | In progress; no profile writes, cache fallback/interop tests next |
| F05.state_ownership | Hydrate/remove original cached properties before durable per-torrent updates | P02-03-torrent-state | Implemented Linux slice; [GCC](evidence/P02-03-torrent-state/gcc-ctest.txt) and [Clang](evidence/P02-03-torrent-state/clang-ctest.txt) actual Java crash/cache/backup reads |
| F25.compatible_state_write | No-gap original torrent state saves with recheck-safe backups and immutable info | P02-03-torrent-state | Implemented Linux slice; same evidence; sanitizer update pending |
| F05.same_profile_local | Original Java partial -> native continuation -> original Java reopen using the same files | P03-01-local-session | Implemented experimental linear v1/v2/hybrid GCC slice; [same-profile handoff](evidence/P03-01/handoff-v1-gcc.json); Clang/sanitizer/crash integration next |
| F05.linear_handoff_recovery | Resume Java/native same-file operation after interrupted completion boundaries | P03-02-local-crashes | In progress; normal handoffs pass, crash/alias cleanup next |
| F01.original_file_geometry | Original v1/hybrid lists and Java-ordered v2 file trees | P03-03-multifile-linear | In progress; matched original three-version fixture geometry, core promotion/testing next |
| F02.java_pad_indices | Original Java virtual pad names, indices and logical byte accounting | P03-03-multifile-linear | In progress; engine pad names differ; never create physical pad files |
| F02.multifile_linear | Nested/Unicode/empty/padded files and original per-file indices/links | P03-03-multifile-linear | In progress; characterize original Java before broadening native activation |
| F05.multifile_same_profile | Original multifile Java/native/Java continuation using unchanged paths | P03-03-multifile-linear | In progress; original fixtures first |
| F02.completion_linear | Durable flush, original incomplete-file links and collision-safe completion | P03-01-local-session | In progress; completion/crash tests pending |
| F23.native_local_cli | Explicit experimental native headless local command | P03-01-local-session | In progress; generated profiles only until broader qualification |
| F02.compact_map | Preserve first/last piece fragments and mark skipped holes missing | P01-02-storage | Experimental; GCC fixtures and [Clang Java interop](evidence/P01-02-storage/clang-ctest.txt) pass seven cases; write integration pending |
| F02.reordered_map | Decode original allocation map, reject invalid aliases and mark unallocated pieces transient | P01-02-storage | Experimental; same seven cases; allocation/relocation and engine disk integration pending |
| F01.gzip_torrent | Read Java-compatible gzip torrent envelopes without changing hashes | P02-03-state-codec | In progress; bounded codec and malformed-stream tests pass GCC, Java/Clang qualification pending |
| F05.state_cache_gzip | Preserve compressed global state cache, including unknown attributes | P02-03-state-codec | In progress; codec only; cache coherency remains an integration gate |
| F01.encoding | Preserve untouched torrent info bytes in `formats.bencode` | P01-03 | Implemented slice; Java differential and [interop](evidence/P01-03/gcc-interop.json) |
| F25.unknown_fields | Opaque binary/nested config fields survive edits in `formats.bencode` | P01-03 | Implemented slice; same interop evidence |
| F25.compatible_save | Original config, `.saving` and `.bak` save/recovery in `profile.config` | P02-03 | Implemented Linux slice; [Java recovery tests](evidence/P02-03/gcc-ctest.txt) |
| F26.profile_lock | Original `.azlock` conflicts with Java and native writers | P02-03 | Implemented Linux slice; same Java recovery/lock tests |
| F27.linux_deb | Linux DEB variants with private Qt initially and system Qt later | P02-01-linux-deb | In progress; six-method packaging contract and real Qt relocation fixture pass; full client installation/release remains pending |
| F30.safe_paths | Reject unsafe config filenames, symlinks and stale replacement | P02-03 | Implemented slice; same tests; full payload path safety pending |
| F30.bounded_parser | Explicit input/string/node/depth limits, no ambiguous encodings | P01-03 | Implemented slice; 20k deterministic trials and Java fixture classification |
| F28.lifecycle | Explicit startup, resolved futures and worker final cleanup in `runtime.executor` | P02-02 / [P02-08](tasks/P02-08-executor-finalizer.md) | Implemented bounded foundation; legacy assertions plus7 native cases/3CTest on GCC/Clang/ASAN+UBSAN+LSAN/TSAN. [Matrix](evidence/P02-08-executor-finalizer/matrix.json). Graceful persistence/Close remains separate. |
| F28.native_module_abi | C ABI, capability/version/lifetime and independently built native modules | P02-05-plugin-modules | Bounded implementation and scoped qualification; production host/ABI closure pending |
| F27.native_plugin_activation | Signed immutable plugin activation, interruption, retry and rollback beside Java JARs | P02-05-plugin-modules | Offline generated-profile implementation; production updater/trust remains pending |
| F31.location_provider | Original MMDB country and loose/JAR flags with host localization | P02-05-plugin-modules | Native azlocprov module; broader locale/Java dependency/tag-example closure pending |
| F04.mainline_restart | Disposable native restart state and original read-only routing hints | [P05 restart](tasks/P05-MAINLINE-RESTART.md) | Actual state-only KRPC restart/ID flag and cache/CAS qualification passed; original Java identity/writeback and broad interoperability remain open |
| F04.mainline_discovery | Independent Mainline port/settings and current managed/magnet peer acquisition | [P05 Mainline](tasks/P05-MAINLINE-DISCOVERY.md) | Owned dual-family original identities/routing, default-interface planning and saved-only UDP/HTTP(S) scrapes qualified at `210148e403a9e3b0b539d0c195a44e5bb661ba6c`; [executed scope](evidence/20261029-mainline-owned-trackers/REVIEW.md). Multiple sockets per family, broader original tracker dialects, proxy/public interoperability and performance remain open. |
| F04.saved_tracker_credentials | Original saved Basic credentials and JKS trust | [Tracker transport evidence](evidence/20261029-http-auth-trust/REVIEW.md) | Source `c7a6ab6700666b86cbf2a0a265cb95addb57a660`: focused GCC/Clang/SAN 3/2/3 suites, 28 HTTP Qt rows per runtime lane and two private HTTPS checks. ASCII Basic/JKS subset; other formats, proxy/auth schemes and original permissive TLS remain open. |
| F06.mldht_host_policy | Mainline bind address and per-download announce override semantics | P02-05-plugin-modules | Internal policy port;4860 extracted-original observations, no DHT network claim |
| F25.mldht_identity | Preserve original baseID.config selection/encoding and Java reopening | P05-02-mldht-persistence | Original codec's117 cases/104 Java roundtrips retained. Held-profile Host integration, absent-only canonical root creation and shared original root with separate IPv4/IPv6 wire authority qualified at `210148e403a9e3b0b539d0c195a44e5bb661ba6c`; [limits](evidence/20261029-mainline-owned-trackers/REVIEW.md). Full routing/writeback and public interoperability remain open. |
| F25.mldht_routing_cache | Decode original Mainline routing cache with preserved unknown/binary data | P05-02-mldht-persistence | Implemented bounded adapter;64 original cases and59 Java roundtrips per compiler; network activation remains pending |
| F25.original_plugin_config | Original folder-derived prefixes, runtime startup flags and typed defaults | P02-06-plugin-settings | Implemented bounded reader;90 original snapshots pass GCC/Clang/sanitizers; lifecycle/editing remain pending |
| F30.bounded_executor | Queue/cancellation, caller cleanup barrier and affine finalization | [P02-08](tasks/P02-08-executor-finalizer.md) | Implemented bounded foundation; concurrent/reentrant/full-queue/startup-failure cases and retained legacy assertions pass all four lanes. [Review](evidence/P02-08-executor-finalizer/review.md). |
| F11.legacy_signatures | Legacy prime192v2 key encoding and SHA1/ECDSA interoperability | P01-03 | Experimental `crypto.legacy_identity`; [Java/native crypto](evidence/P01-03/clang-interop.json); identity lifecycle pending |
| F30.crypto_provider | Use vetted OpenSSL EVP checks; reject malformed keys/signatures | P01-03 | Experimental; same crypto evidence; provider delivery pending |

## External Plugin and Engine Coverage

Every repository below must have a disposition, including replacements and retirement. Proposed retirement is not completion until the user accepts it and its replacement/migration implications are tested.

| ID | Source registry ID | Feature families | Phase | Planned treatment |
| --- | --- | --- | --- | --- |
| X01 | `SpeedScheduler` | F07, F13, F15, F19 | P04/P08 | Port scheduling/XML semantics and Qt editor |
| X02 | `aercm` | F09, F12, F16, F19 | P06/P08 | Native related-content service integration and views |
| X03 | `azemp` | F19, F24 | P09 | Native media integration and controls; replace Java/SWT bindings |
| X04 | `azlocprov` | F20, F31 | P04 | MMDB adapter, flags and localization |
| X05 | `azmsgsync` | F09, F10, F11 | P06/P07 | Compatible message synchronization and crypto protocols |
| X06 | `azneti2phelper` | F06, F09, F11 | P07 | Proven native router/client adapter plus I2P-specific services |
| X07 | `aznettor` | F06, F26 | P07 | Native process/control/proxy/onion-service integration |
| X08 | `azrating` | F10, F11, F12, F20 | P06/P07 | Distributed ratings, optional chat integration and Qt UI |
| X09 | `azupnpav` | F24 | P09 | Media discovery/content directory/range streaming |
| X10 | `azutp` | F03 | P05 | Selected native uTP implementation with local-fix/interoperability review |
| X11 | `dorkboxupdater` | F26, F27 | P11 | Retirement accepted 2026-10-17, [AD-007](decisions/AD-007.md); no native release/download, preserve Java files/settings; native tray remains |
| X12 | `mlDHT` | F04, F20 | P05 | Host integration and stats around selected Mainline DHT engine |
| X13 | `mlab` | F29 | P10 | Explicit, supported network-measurement integration |
| X14 | `vuzexcode` | F24 | P09 | Process supervision, profiles, progress and cancellation |
| X15 | `xmwebui` | F16, F22, F23 | P10 | RPC contracts, extensions, pairing/tunnels and reusable web assets |
| X16 | `azupdater` | F27 | P11 | Native update/restart helper; remove Java-path patching from design |
| X17 | `mldht` | F04 | P05 | Native engine behavior replacement; retain separate Java-engine provenance |

## Completion Rules

- Parent rows remain partial until their enumerated behaviors and platform variants are verified. A feature tested on Linux is not automatically verified on Windows/macOS.
- Keep library/API-only behavior visible even if there is no UI screen for it. Keep UI behavior visible even if the core already works.
- An intentionally different implementation can satisfy parity when behavior and performance are demonstrated; a different user-visible behavior requires a recorded decision.
- Missing upstream sources, unavailable services, unresolved dependency versions, and untested live-network cases remain explicit limitations with tasks. They do not count as successful parity.
- P12 compares this expanded matrix with the frozen inventory and subsequent discoveries. Completion is judged against behaviors and evidence, not percentages of Java files translated.

| F12.rating_data | Original azrating key, payload and aggregate model | P06-04-rating-data | Implemented bounded model;1819 original observations and366 Java payload roundtrips per compiler; DDB/UI integration pending |

| F06.tor_control_auth | Original Tor SAFECOOKIE server/controller proof | P07-01-tor-auth | Implemented bounded proof library;1024 original transcripts pass GCC/Clang/sanitizers; controller/process activation pending |

| F06.tor_control_channel | Qt asynchronous original Tor control authentication/framing | P07-01b-tor-channel | Implemented bounded channel; GCC/Clang/leak-enabled sanitizer owned-loopback tests pass; process/proxy activation pending |

| F11.msgsync_envelope | Original azmsgsync key derivation and encrypted envelopes | P06-03-message-envelope | Implemented bounded key/envelope codec;463 original cases and136 Java payload readbacks per compiler; full message/identity service pending |

| F22.original_rpc_contract | Original xmwebui RPC envelopes and session checks | P10-01-rpc-contract | Implemented bounded wrapper; 804 original observations pass GCC/Clang/sanitizers; listener and real methods pending |

| F22.original_rpc_ids | Preserve original RPC IDs, duplicate repair and allocation | P10-02-rpc-identities | Implemented bounded planner; 828 cases/40,824 original lookups per compiler; file journal/host binding pending |

| F25.original_plugin_attributes | Cached/primary plugin Long attributes and full64 config | P02-07-plugin-attributes | Implemented bounded reader; 952 original cases plus settings/cache/core regressions pass all three compilers; host file/provider binding pending |

| F22.original_rpc_profile_data | Original persisted active/archive IDs and counter from one lease | P10-03-rpc-profile-data | Bounded provider implemented;38 getter-backed filesystem snapshots and12 regressions pass GCC/Clang/ASan+UBSan+LSan; registration/journal/runtime wiring pending |

| F12.rating_local_policy | Original local rating load/store and ordered publication intentions | P06-06-rating-local-policy | Bounded policy implemented;70000 original observations incl all UTF-16 units and rating/readback regressions pass GCC/Clang/sanitizers; actual host writer/DDB/UI pending |
| F12.rating_publication_queue | Ratings publication queue policy | [P06-29](tasks/P06-29-rating-publication.md) | Implemented bounded policy;653 source comparisons,386 native assertions and112 CLI observations/14 cases per GCC/Clang/ASAN+UBSAN+LSAN lane.193 source-only/zero credit. Actual DDB/timer/host activation pending. |

| F11.msgsync_sts | Original legacy identity and authenticated STS key agreement | P06-07-msgsync-sts | Bounded primitive implemented;65 mutual handshakes/780 private readbacks per compiler incl25-/49-byte keys and23-/24-/25-byte secrets; explicit invalid-point rejection, host/DHT activation pending |

## Bounded Azureus DHT and Tor ownership rows

These stable IDs match the source-lock components. Linked P06-08 through P06-24
and P07-01c matrices record scoped Linux GCC 16, Clang 24 and sanitizer checks.
The rows remain bounded implementations; routing, persistence, normal application
activation, full phase/release verification and performance claims remain outside
what those checks establish. P06-25 qualifies generated authority/admission serving; actual DB/router activation remains separate.

| ID | Behavior and native component | Task | Status and evidence |
| --- | --- | --- | --- |
| F09.udp_headers | UDP headers/endpoints (`azureus_dht`) | [P06-08-azureus-dht-headers](tasks/P06-08-azureus-dht-headers.md) | Implemented bounded slice; Header/endpoint wire fixtures, bounds and source readbacks; no routing. [Matrix](evidence/P06-08-azureus-dht-headers/matrix.json), [review](evidence/P06-08-azureus-dht-headers/review.md). |
| F09.ping_options | PING request options (`azureus_ping`) | [P06-09-azureus-ping-options](tasks/P06-09-azureus-ping-options.md) | Implemented bounded slice; Alternative requests/upload-stat wire rules and source readbacks; no live statistics policy. [Matrix](evidence/P06-09-azureus-ping-options/matrix.json), [review](evidence/P06-09-azureus-ping-options/review.md). |
| F09.positions_wire | Network-position wire (`azureus_positions`) | [P06-10-azureus-positions](tasks/P06-10-azureus-positions.md) | Implemented bounded slice; Vivaldi framing/provider serialization and source readbacks; no coordinate algorithm. [Matrix](evidence/P06-10-azureus-positions/matrix.json), [review](evidence/P06-10-azureus-positions/review.md). |
| F09.alt_contacts_wire | Alternative-contact wire (`azureus_contacts`) | [P06-11-azureus-alternative-contacts](tasks/P06-11-azureus-alternative-contacts.md) | Implemented bounded slice; Opaque properties, identity/age framing and source readbacks; no provider/cache activation. [Matrix](evidence/P06-11-azureus-contacts/matrix.json), [review](evidence/P06-11-azureus-contacts/review.md). |
| F09.ping_packets | Full PING packets (`azureus_packets`) | [P06-12-azureus-ping-packets](tasks/P06-12-azureus-ping-packets.md) | Implemented bounded slice; One-stream request/reply codecs and source readbacks; no transport scheduling. [Matrix](evidence/P06-12-azureus-packets/matrix.json), [review](evidence/P06-12-azureus-packets/review.md). |
| F09.owned_udp | Owned PING UDP actor (`azureus_udp`) | [P06-13-azureus-owned-udp](tasks/P06-13-azureus-owned-udp.md) | Implemented bounded slice; Bounded IPv4/IPv6 peer/binding/deadline/callback behavior and Java peer datagrams; loopback only. [Matrix](evidence/P06-13-azureus-owned-udp/matrix.json), [review](evidence/P06-13-azureus-owned-udp/review.md). |
| F09.node_identity | Node identity/cache (`azureus_nodes`) | [P06-14-azureus-node-contacts](tasks/P06-14-azureus-node-contacts.md) | Implemented bounded slice; Original restricted/version-dependent IDs, bounded cache and failure behavior; no routing table. [Matrix](evidence/P06-14-azureus-nodes/matrix.json), [review](evidence/P06-14-azureus-nodes/review.md). |
| F09.contacts_wire | Regular contact wire (`azureus_nodes`) | [P06-14-azureus-node-contacts](tasks/P06-14-azureus-node-contacts.md) | Implemented bounded slice; Single/array contacts, source filtering and Java readbacks; no contact-history activation. [Matrix](evidence/P06-14-azureus-nodes/matrix.json), [review](evidence/P06-14-azureus-nodes/review.md). |
| F09.find_node_wire | FIND_NODE wire (`azureus_find_node`) | [P06-15-azureus-find-node](tasks/P06-15-azureus-find-node.md) | Implemented bounded slice; Full request/reply, positions/contacts and source readbacks; no lookup controller. [Matrix](evidence/P06-15-azureus-find-node/matrix.json), [review](evidence/P06-15-azureus-find-node/review.md). |
| F09.owned_find_node | Owned FIND_NODE actor (`azureus_udp`) | [P06-16-owned-find-node](tasks/P06-16-owned-find-node.md) | Implemented bounded slice; Typed PING/FIND_NODE ownership and actual Java peer datagrams; no iterative routing. [Matrix](evidence/P06-16-owned-find-node/matrix.json), [review](evidence/P06-16-owned-find-node/review.md). |
| F09.values_wire | Transport-value wire (`azureus_values`) | [P06-17-azureus-transport-values](tasks/P06-17-azureus-transport-values.md) | Implemented bounded slice; Value arrays, skew/raw control bytes and source readbacks; no DB lifecycle/storage. [Matrix](evidence/P06-17-azureus-values/matrix.json), [review](evidence/P06-17-azureus-values/review.md). |
| F09.store_wire | STORE wire (`azureus_store`) | [P06-18-azureus-store-packets](tasks/P06-18-azureus-store-packets.md) | Implemented bounded slice; Independent keys/value sets, diversification/skew and source readbacks; no admission/server authorization. [Matrix](evidence/P06-18-azureus-store/matrix.json), [review](evidence/P06-18-azureus-store/review.md). |
| F09.find_value_wire | FIND_VALUE wire (`azureus_find_value`) | [P06-19-azureus-find-value-packets](tasks/P06-19-azureus-find-value-packets.md) | Implemented bounded slice; Value/contact modes, limits/continuation and source readbacks; no DB lookup. [Matrix](evidence/P06-19-azureus-find-value/matrix.json), [review](evidence/P06-19-azureus-find-value/review.md). |
| F09.owned_find_value | Owned continuation actor (`azureus_udp`) | [P06-20-owned-find-value-stream](tasks/P06-20-owned-find-value-stream.md) | Implemented bounded slice; Ordered parts, fixed deadline/cancellation/capacity and Java stream datagrams; no DB/lookup engine. [Matrix](evidence/P06-20-owned-stream/matrix.json), [review](evidence/P06-20-owned-stream/review.md). |
| F09.anti_spoof | Anti-spoof authority (`azureus_auth`) | [P06-21-azureus-authority](tasks/P06-21-azureus-authority.md) | Implemented bounded slice; Private token issue/verify/cache and Java checks; standalone primitive, no admission activation. [Matrix](evidence/P06-21-azureus-authority/matrix.json), [review](evidence/P06-21-azureus-authority/review.md). |
| F09.store_admission | STORE admission intentions (`azureus_store_policy`) | [P06-22-azureus-store-admission](tasks/P06-22-azureus-store-admission.md) | Implemented bounded slice; Original gate/event order, metadata/block/memo decisions and fresh source comparisons; no DB/router writes. [Matrix](evidence/P06-22-azureus-store-policy/matrix.json), [review](evidence/P06-22-azureus-store-policy/review.md). |
| F09.error_packets | ERROR reply wire (`azureus_error`) | [P06-23-azureus-error-replies](tasks/P06-23-azureus-error-replies.md) | Implemented bounded slice; Typed/unknown ERROR bodies, partial output, bounds and Java readbacks; no trust/host error effects. [Matrix](evidence/P06-23-azureus-error/matrix.json), [review](evidence/P06-23-azureus-error/review.md). |
| F09.owned_store_and_errors | Owned STORE/detailed ERROR (`azureus_udp`) | [P06-24-owned-store-and-errors](tasks/P06-24-owned-store-and-errors.md) | Implemented bounded slice; Single-datagram STORE, terminal ERROR/continuation and Java peers with inert replies; authority/admission integration belongs to P06-25. [Matrix](evidence/P06-24-owned-store/matrix.json), [review](evidence/P06-24-owned-store/review.md). |
| F09.owned_store_authority | Owned authority/admission service (`azureus_udp`) | [P06-25-owned-authority](tasks/P06-25-owned-authority.md) | Implemented bounded generated service;31 CTest/103 Qt checks,840 new datagrams and210 reverse gate comparisons per compiler;1600 total datagrams and all six lower readbacks pass. [Matrix](evidence/P06-25-owned-authority/matrix.json), [review](evidence/P06-25-owned-authority/review.md). Actual DB/router activation remains separate. |
| F09.db_value_lifecycle | Original DB value lifecycle with owned payload/contact handles (`azureus_db_value`) | [P06-26](tasks/P06-26-azureus-db-value.md) | Implemented bounded lifecycle;6767 semantic matches,577 source-only rows with zero equality credit; seven CTest entries include unchanged values/nodes on all three toolchains. [Matrix](evidence/P06-26-azureus-db-value/matrix.json), [review](evidence/P06-26-azureus-db-value/review.md). No DB mapping/persistence activation. |
| F09.router_contact_lifecycle | Original router-contact state (`azureus_router_contact`) | [P06-27](tasks/P06-27-azureus-router-contact.md) | In progress: GCC/Clang10/10 pass; ASAN9/10 in both retained attempts, unchanged DB CLI120s timeout unresolved. New router semantic/ownership/CLI tests pass;1933 source-only rows retain zero credit. [Partial results](evidence/P06-27-router-contact/partial-results.json). No full matrix or activation claim. |
| F09.db_mapping | Original one-key DB mapping (`azureus_db_mapping`) | [P06-28](tasks/P06-28-azureus-db-mapping.md) | In progress; reviewed owned native code/tools adopted, unbuilt.5626 original observations/40 bodies preserved;2745 comparison candidates,2881 source-only/zero credit. Fresh20-class proof and three-lane qualification pending. [Source fixtures](fixtures/azureus-db-mapping/manifest.json), [verification](evidence/P06-28-db-mapping/source-freeze-verification.json). |
| F06.tor_control_ownership | Post-auth Tor ownership (`tor_control`) | [P07-01c-tor-ownership](tasks/P07-01c-tor-ownership.md) | Implemented bounded slice; TAKEOWNERSHIP/RESETCONF/GETINFO version, source projection and Qt lifecycle; no process/proxy/onion activation. [Matrix](evidence/P07-01c-tor-ownership/matrix.json), [review](evidence/P07-01c-tor-ownership/review.md). |

Standalone channel teardown follow-up: [P07-01d](tasks/P07-01d-tor-channel-teardown.md) passes five CTest rows/167 aggregate Qt checks per compiler, with four new destruction rows and unchanged old24/auth/ownership cases.


## Persistent session continuation

| Feature | Component | Task | Current evidence and remaining work |
| --- | --- | --- | --- |
| F05.persistent_owned_local | Persistent local engine owner | [P03-05](tasks/P03-05-persistent-session.md) | In progress;14,068 original export/pause observations frozen with67 expected source failures. Completion source now freezes2,280 observations with22 actual classes and46 expected outward faults; export/pause rebuild preserves17 classes. Owner shutdown, storage barriers and actual continued seeding remain pending. No native implementation or equality credit. [Phase1 verification](evidence/P03-05-persistent-session/phase1-source-freeze-verification.json). |


## Classic preview continuation

| Feature | Component | Task | Current evidence and remaining work |
| --- | --- | --- | --- |
| F19.classic_navigation | Original-named My Torrents/Ctrl+1 and saved My Tracker/My Shares tabs | [P04-03](tasks/P04-03-classic-navigation.md), [P04-05](tasks/P04-05-saved-service-views.md), [P04-07](tasks/P04-07-classic-close-tab.md) | Bounded preview checks pass; retained-widget/Qt tab selection are native adaptations. Full original view lifecycle/plugin/tray/persistence remains open. |
| F19.classic_column_controls | Per-pane session column chooser/reset | [P04-04](tasks/P04-04-classic-columns.md) | Tested preview controls; original saved layout compatibility remains open. |
| F19.saved_queue_position | Raw saved position column | [P04-06](tasks/P04-06-saved-queue-position.md) | Sourceec56329fdfab1a45a8eafd7bacc97c163d5815c2; not active scheduler Rank. |
| F05.saved_dnd_aggregates | Fixed saved skipped-file facts and strict selected metrics | [P04-08](tasks/P04-08-saved-dnd-aggregates.md) | Sourceaaa7a1f4a84b66c0edc3d900c701e38a81fcfd2b;33 units,61 cases/464 assertions across3 lanes and hosted checks pass. No manager/runtime equality or per-file recovery credit. |
| F19.saved_selected_values | Optional labelled saved selected columns | [P04-09](tasks/P04-09-saved-selected-columns.md) | Source03e31e5ce260e1327e63cca63d22d8d5984ceae9;34 local/hosted units,26newQtrows and focused sanitizers pass; appended hidden columns use current cache/raw saved counters only. Pane policy/live progress remain separate. |

These partial component records preserve full original/native hashes in the
registry without advancing full integration baselines or altering older frozen
qualification states. Public source and local generated evidence are distinct.


| Feature | Component | Task | Current evidence and remaining work |
| --- | --- | --- | --- |
| F19.classic_column_layouts | Original layout facts, conservative import and native explicit persistence | [P04-10](tasks/P04-10-classic-column-layout-projection.md), [P04-13](tasks/P04-13-classic-layout-persistence.md), [P04-14](tasks/P04-14-classic-layout-first-save.md) | Source8efe56e4c0a4c46b8cdcb51035364db938494bb5 adds qualified no-overwrite first-save to44 local/hosted units,3scale smoke and focused Clang/leak-enabled sanitizer/storage regressions. Existing Java records preserved; original registered/shared layout writing and full runtime parity remain open. |
| F20.classic_statistics | Tools > Statistics / Ctrl+5 | [P04-15](tasks/P04-15-classic-statistics.md) | First bounded view published at `be9ad3d5f409947445b6c253b17430a9df0f6655`:13 original saved fields, bounded payload Activity,55 Qt rows,48 units and three-scale smoke. Global/session/history and other original subviews unfinished. |

| F20.engine_session_counters | Measured scoped engine counters/rates | [P04-16](tasks/P04-16-engine-statistics.md) | Bounded implemented source `5fc34b588713112b126f3cebdfc891020301a281`:50 local/hosted units,3 Clang/9 leak-enabled sanitizer suites,92 Page+12 actual-engine Qt rows and 24 three-scale captures pass. One operation/session; global/lifetime/history, exhaustive final totals and full runtime parity remain open. |

| F20.engine_activity | Measured engine traffic graphs | [P04-17](tasks/P04-17-engine-activity.md) | Bounded implemented source `a84e4ecc6f78112c115c182aefc13bc4a1f3ac17`:53 local/hosted units,5 Clang/9 leak-enabled sanitizer suites,52History/26View/7real graph Qt rows, retained92Page/12engine rows and30 three-scale captures pass. At most600 actual per-operation rate points with gaps; global/persistent history and full runtime parity remain open. |

| F20.saved_history_day | Read-only saved UTC daily traffic history | [P04-18](tasks/P04-18-saved-history.md) | Bounded implemented source `9db774659334daa2dbabd81762c819c108eb43b2`: 57 local/hosted units, 8 Clang and 11 leak-enabled sanitizer suites; 21 projection and 11 reader cases, 17 actual Java fixtures, 425 queries and 2550 scalars, 34 History UI and 12 worker Qt rows and 36 three-scale captures pass. Saved UTC day accounting only; rolling graphs, clock-clamp/strict-error parity, cache/writer/global and full runtime work remain open. |

| F20.saved_history_week | Saved seven-day overview of independent UTC daily records | [P04-19](tasks/P04-19-saved-history-week.md) | Bounded implemented source `136af049bc3ea5307a666ef4b716eaa50d36d1c4`: 61 local/hosted units, 12 Clang and 15 leak-enabled sanitizer suites; 9 date cases, 12 reader families, 525 new original-Java queries/3150 scalars, 38 UI/12 worker Qt rows and 42 three-scale captures pass. All prior 57 units and Daily 34/12 rows retained. Seven independent daily results only; no weekly total, rolling graph or full-runtime parity. |

| F21.saved_history_charts | Saved payload charts for fixed UTC History buckets | [P04-20](tasks/P04-20-saved-history-charts.md) | Bounded implemented source `889dfe8cc9a22830149d79c7108ce946a7df2766`: 64 GCC/hosted, 15 Clang and 18 leak-enabled sanitizer suites; actual data/widget/view Qt rows 19/13/22; 42 local captures. All prior 61/12/15 names, accounting and 2550/3150 scalar corpora retained. Rendering only; no new Java equality, rolling graph, weekly sum or global history parity. |

| F21.saved_history_hourly | Selectable saved seven-day hourly payload chart | [P04-21](tasks/P04-21-history-hourly.md) | Bounded implemented source `c35ca6e2470aa8f154ec7676e61734c012725283`: 65 GCC/hosted, 16 Clang and 19 leak-enabled sanitizer suites; actual new hourly Qt rows 22; 48 local captures. All prior 64/15/18 names, chart rows 19/13/22, accounting and 2550/3150 scalar corpora retained. Fixed selected UTC 168 hours, local rendering preference; no new reads/Java equality, rolling query, weekly sum or global history parity. |

| F21.statistics_navigation | Remember Statistics tabs, dates and resolution when reopening | [P04-22](tasks/P04-22-statistics-navigation.md) | Bounded implemented source `f7d979801773934d481155019b18319b64beff96`: 67 GCC/hosted, 18 Clang and 21 leak-enabled sanitizer suites; actual Page/Window Qt rows 13/11; 54 local captures. All prior 65/16/19 names, hourly 22, chart rows 19/13/22, accounting and 2550/3150 scalar corpora retained. Session-tagged per-Window scalar memory, fresh Page/reads; closed memory clears on new session, visible-Page behavior unchanged. No retained widget/results, configuration persistence or Java UI equality. |

| F21.history_traffic | Saved History traffic selection | [P04-23](tasks/P04-23-history-traffic.md) | Bounded implemented source `1b08cceee82df9d29f1b35719a6ac24f2c6c2bc1`: 69 GCC/hosted, 20 Clang and 23 leak-enabled sanitizer suites; actual traffic data/View Qt rows 10/14; 60 local captures. All prior 67/18/21 names, navigation 13/11, hourly 22, chart rows 19/13/22, accounting and 2550/3150 scalar corpora retained. Payload/Protocol/DHT copies exact saved fields, with unchanged tables and no extra reads. No combined sums, rates, weekly total or new Java UI equality. |

| F21.history_keyboard | Keyboard inspection of saved History charts | [P04-24](tasks/P04-24-history-keyboard.md) | Bounded implemented source `07219078bf3c5b3ed1cca7e701dcbdfeda8e687d`: 70 GCC/hosted, 21 Clang and 24 leak-enabled sanitizer suites; actual keyboard Qt rows 17; 63 local captures. All prior 69/20/23 names, traffic 10/14, navigation 13/11, hourly 22, chart rows 19/13/22, accounting and 2550/3150 scalar corpora retained. Focused Left/Right/Home/End select bounded exact UTC buckets with signed/Unknown narration and callback fences; no new reads, timers, config persistence or Java UI equality. Copy remains P04-25. |

| F21.history_copy | Copy selected History chart values | [P04-25](tasks/P04-25-history-copy.md) | Bounded implemented source `ab0ff75d81300d8d3f9e930a9bf55491c1801400`: 71 GCC/hosted, 22 Clang and 25 leak-enabled sanitizer suites; actual Copy Qt rows 19; 66 local captures. All prior 70/21/24 names, keyboard 17, traffic 10/14, navigation 13/11, hourly 22, chart rows 19/13/22, accounting and 2550/3150 scalar corpora retained. Explicit widget-local Copy/context action publishes exact UTC/traffic/signed-or-Unknown selected values only to Clipboard; no automatic changes, new saved reads/config writes or Java UI equality. |

| F20.saved_history_month | Read-only fixed 30-day saved-history backend | [P04-26](tasks/P04-26-saved-history-month.md) | Bounded implemented `d36dd2d1c92914c796e20115939e3c66e7627ba1`; 73/24/27 GCC/Clang/sanitizer suites; saved_history_month_date_cases=11, saved_history_month_reader_cases=14; 66 local/22 hosted captures. Thirty independent UTC daily records; backend only; UI tracked separately in P04-27. No period aggregate, original 720-hour graph equality or broad integration advancement. |

| F21.history_month_ui | Statistics: 30 independent saved UTC days | [P04-27](tasks/P04-27-history-month-ui.md) | Bounded implemented `ddc6bb022095866eceeddf2d8598201e07652c28`; 76/27/30 GCC/Clang/sanitizer suites; saved_history_month_plot_qt_rows=13, saved_history_month_ui_qt_rows=25, saved_history_month_window_worker_qt_rows=12; 69 local/23 hosted captures. Thirty independent UTC daily records; real saved 30-day tab/traffic/inspection/Copy; No period aggregate, original 720-hour graph equality or broad integration advancement. |

| F20.rolling_history | Statistics rolling Hour/Day/Week/Month/Year/All | [P04-28](tasks/P04-28-rolling-history.md) | Qualified/published: Hour/Day/Week/Month/Year/All held-profile query and worker/UI;7276 original-Java queries match43656 scalar fields,83/34/37 suites and78 local/26 hosted captures pass. Scope remains read-only; recording and full Statistics stay open. |

| F20.persistent_peer_accounting | Persistent native peer owner prerequisite | [P04-29](tasks/P04-29-persistent-peer-accounting.md) | Bounded backend source `b0c7f259bca2043df4fb1297e3440f2e5eb1a270` qualified: DEFAULT85, private GCC15/Clang49/SAN52,22 negatives,5 generated Java handoffs. Portable default remains per-operation fallback. Broad original Statistics mapping, new widgets and writers remain unfinished. |
