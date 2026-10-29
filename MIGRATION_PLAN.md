# Whole Solution C++ Migration Plan

Continue the entire plan autonomously without chapter or milestone stops, as requested on 2026-10-28. Preserve the running Java application and real profile/payload. Read [the compact active state](migration/ACTIVE.md) first; batch related implementation and focused validation, retain build logs and inspect failures/status only.

Latest qualified native source `6726a4cf7b017ac31d6aeede15f339c3ec2b71f7` adds normal direct DHT receiving and owned LeaseSet2 submission; seven affected suites passed on three compiler/sanitizer lanes, plus private runtime and actual Java-object checks. Whole-application products were freshly built on GCC only. See [current state](migration/STATE.md); whole migration and release/performance gates remain open.

Earlier progress and stop-boundary notes are preserved verbatim in [the historical status archive](migration/history/plan-status-prefix-through-20261028.md); those stops are superseded. The objective, phase definitions and acceptance requirements below are unchanged.

## Objective and Completion Boundary

Replace the Java/SWT application and the functionality of its listed plugins with a native C++ application that uses less CPU and memory at equivalent functionality. Preserve torrent data, protocol interoperability, user workflows, and a traceable relationship to upstream source. The working UI choice is Qt 6.12.0 Widgets, with a local SDK at `/opt/Qt/6.12.0/gcc_64`. Use GCC/G++ 16.0.1 (2026-03-22 snapshot) primarily on Linux and Clang 24.0.0 (2026-09-11 snapshot) for secondary qualification, with a shared C++23 baseline and individually gated newer features. Follow the [toolchain requirements and incompatibility policy](migration/TOOLCHAIN_REQUIREMENTS.md): both local compiler/libstdc++ 16 pairs pass the baseline and offscreen Qt smoke checks, but still require full integration and release qualification.

The deliverable is a maintainable replacement, not a line-by-line translation or a native shell around a permanent JVM. Existing Java JARs are not native plugins. Keep the current application operational as the reference implementation throughout migration. This plan authorizes no removal of existing source, installations, or user data.

**User requirements added 2026-10-17:** name the native application **BiglyBTC**;
retain existing plugin names and IDs. Preserve file compatibility with the
original BiglyBT application, including configuration, plugin settings, torrent
metadata, resume state, and incomplete/temporary payload files. Native storage
choices must support Java/native round trips, unknown-field preservation and
rollback without destructive conversion. Use isolated fixture profiles during
development and exclusive payload/profile ownership when switching applications.
These requirements take precedence over the proposed new native formats below.

Native plugin project/repository names use the **BiglyBTC-plugin-** prefix, for
example `BiglyBTC-plugin-mlDHT`. Plugin display names and runtime IDs remain
compatible. Preserve the original Java repositories and canonical source URLs as
upstream references; this naming convention does not rename those checkouts.

**Linux delivery decision, 2026-10-17:** concentrate on a small Linux `.deb`
with two variants: privately bundled Qt initially, and small system-Qt packages
when matching Qt Debian packages exist. Other ordinary dependencies remain system
supplied. Windows/macOS qualification is
deferred beyond the first Linux release. The finished application uses the same
original profile, normally `~/.biglybt`, and existing plugin directories; native
artifacts live in a `biglybtc/` child beside the Java JARs, with separate native
version metadata. Preserve Java descriptors, shared settings and disabled flags.
Use GitHub Releases for native assets with a signed compatibility catalogue;
repository ownership, update trust and runtime delivery still need qualification.
See [AD-006](migration/decisions/AD-006.md) for source findings, layout and gates.

The user clarified the final acceptance test: open and close the original Java
application, then open BiglyBTC against the same profile and payload files and
continue seamlessly with equivalent behavior and better performance. Switching
back must also work. An import/export ceremony or a permanently converted profile
does not satisfy this requirement. Development remains on isolated copies until
the full switching, recovery, exclusivity and rollback gates pass.

Full completion means every feature in the approved scope has passed its acceptance tests, all supported platforms have release evidence, migration and rollback have been exercised, resource targets have been met, and an upstream-update rehearsal has succeeded. An MVP, a visual demo, or standard BitTorrent support alone is not whole-solution parity. Feature removal, permanent deferral, a changed privacy model, or dropping an OS requires an explicit user decision.

**Execution status:** in progress. Bounded native implementations and their
qualification checkpoints are recorded in [current state](migration/STATE.md)
and [feature coverage](migration/FEATURE_MATRIX.md); no whole-client migration
or release gate is complete. [Execution gates](migration/EXECUTION_GATES.md)
assigns remaining decisions and validation work; [UI qualification](migration/UI_QUALIFICATION.md)
defines backend, popup, accessibility and resource tests.

**Latest user instruction (2026-10-28):** continue autonomously without stopping through the entire plan. Batch related implementation and focused checks to conserve tokens; retain full build output in durable files and inspect only relevant failures. Preserve the running original Java client and real profile. Earlier chapter and milestone stop boundaries below are historical.

**Historical autonomous authorization:** continue the authorized work until the user says
they have returned, using the current model and effort. Record unresolved
questions in [QUESTIONS.md](migration/QUESTIONS.md) and technical blockers in
the affected task, then advance independent chapters. A completed bounded
component is a checkpoint. It does not complete this plan. Preserve the running
original Java client and its live profile and payloads. Source, drafts, results
and continuation records belong on persistent storage; `/tmp` is disposable
working space with verified persistent copies for fenced reference helpers.
Follow [recovery instructions](migration/RECOVERY.md) after interruption.

## Agent Entry Points

Read in this order; load only the assigned phase and source slices after the shared rules:

1. [Repository guidelines](AGENTS.md) and [current migration state](migration/STATE.md).
2. This plan's architecture, dependency rules, and assigned phase.
3. [Feature coverage](migration/FEATURE_MATRIX.md) and the relevant [plugin audit](PLUGIN_MIGRATION_AUDIT.md) entries.
4. [Upstream tracking rules](migration/UPSTREAM_TRACKING.md), [source lock](migration/upstreams.lock.json), and the assigned task card based on [the template](migration/TASK_TEMPLATE.md).
5. [Toolchain requirements](migration/TOOLCHAIN_REQUIREMENTS.md) before native configuration, compilation, or adoption of a new C++ feature.
6. [Execution gates](migration/EXECUTION_GATES.md) for decision ownership and prerequisites; [UI qualification](migration/UI_QUALIFICATION.md) for P01-01/P04/P12 UI tasks.

The source lock records 18 inspected repositories: BiglyBT plus 17 plugin/engine
repositories. Its component entries distinguish source audits, bounded native
implementations and integration state, with actual native commits and evidence.
An audit hash must never be presented as a completed migration hash.

## Proposed Architecture

Keep one native process for ordinary desktop operation, with a separately runnable headless host using the same services. This does not require running a daemon alongside the GUI. Start Tor, I2P, media, and updater helpers only when needed; their resource use counts toward the application budget.

```text
Qt Widgets desktop                 Headless CLI and remote HTTP service
        \                                      /
         Application commands, queries, events and capability registry
                              |
         Torrent session, storage, policy and domain services
                  /                         \
        Standard torrent engine       BiglyBT-specific services
                  \                         /
          OS, network, persistence and optional helper adapters
```

The proposed implementation tree is `native/` inside this repository during migration. Do not move the Java tree or create another implementation repository until an architecture decision selects that layout. The sibling plugin checkouts remain source references, not build inputs that silently follow their current HEAD.

| Proposed area | Responsibility and boundary |
| --- | --- |
| `native/core/` | Domain types, torrent/session policies, cancellation, configuration contracts, and events; no Qt Widgets or UI-owned objects. |
| `native/adapters/` | Torrent engine, storage, sockets/proxies, crypto, OS services, and subprocess implementations behind explicit contracts. |
| `native/services/` | Tags, scheduling, BiglyBT DHT, messaging, search/subscriptions, devices, and RPC operations. |
| `native/ui/` | Qt models, delegates, windows, actions, graph rendering, and accessibility. |
| `native/apps/` | Desktop/headless composition, startup, shutdown, and instance control. |
| `native/tests/` and `native/benchmarks/` | Deterministic fixtures, unit/integration/UI tests, fault injection, and reproducible workloads. |
| `migration/` | Source provenance, feature/task state, architecture decisions, review records, and compact evidence summaries. |

These are architecture boundaries. Consult the current task records and source
tree for implemented paths and commands; a directory's existence does not close
its feature or integration gates.

### Decisions to Close Before Broad Implementation

Record each decision under `migration/decisions/` with alternatives, evidence, consequences, and a reversal strategy.

- **AD-001 Torrent engine:** prototype reuse of a native engine versus porting BiglyBT's engine. [libtorrent's documented features](https://www.libtorrent.org/features.html) make reuse worth evaluating, but do not establish parity for BiglyBT overlays, swarm merging, queue policy, or all extensions. Produce a feature-by-feature gap map before choosing. Avoid keeping two full engines resident merely to cover gaps.
- **AD-002 UI and toolchain:** use the pinned GCC/G++ 16.0.1 primary and Clang 24.0.0 secondary Linux snapshots in the [toolchain policy](migration/TOOLCHAIN_REQUIREMENTS.md), initially with the installed Qt 6.12.0 SDK. Their paths, C++23/libstdc++ 16 probes, and offscreen Widgets smoke checks are verified; qualify real display-backend interaction, UI resource budgets, complete dependency/runtime combinations, and release-platform builds next. C++23 is the shared baseline; C++26 features require explicit qualification. This decision remains open until the complete toolchain and UI feasibility gates pass, including the production compiler choice.
- **AD-003 Execution and extension model:** assign each mutable subsystem to an executor/thread and define ownership, cancellation, bounded queues, and shutdown order. Initially use internal modules and build-time capabilities. A public dynamic plugin ABI is a separate design decision; do not expose unstable C++ classes or Qt objects as a cross-version ABI.
- **AD-004 Persistence and migration:** retain original-compatible formats, names,
  encodings, resume and temporary-file semantics; define crash consistency,
  unknown-field preservation, schema handling, and secret storage without breaking
  Java interoperability. Prove Java → native → Java round trips on isolated
  profiles. Original user data stays untouched during development. Reading saved
  resume state must not bypass content validation or exclusive ownership.
- **AD-005 Network and crypto:** define bind/proxy/DNS behavior, private-torrent isolation, DHT network separation, and vetted crypto providers. Preserve interoperable wire behavior; do not implement new cryptographic primitives.
- **AD-006 Distribution:** Linux `.deb` first, private Qt bundling initially with a system-Qt variant for later; other ordinary dependencies remain system packages. Windows/macOS qualification is deferred. Follow [AD-006](migration/decisions/AD-006.md) for shared-profile/plugin coexistence, GitHub release/catalogue delivery, distro/architecture qualification, signing/update trust and notices.

### Resource and Ownership Rules

Use RAII and explicit ownership; non-owning references must not outlive their owners. Do not mechanically translate Java object graphs into shared-pointer graphs. Bound caches, pending requests, peer buffers, message histories, log retention, and UI snapshots. Define overload behavior, not just queue capacity.

Network callbacks must not call widgets or block on disk/UI work. Publish batched deltas with stable object IDs and generation checks. Configuration and UI actions validate targets again when executed; a menu may remain open while a torrent disappears. Share immutable backing data where useful without retaining entire old snapshots indefinitely. Coalesce telemetry, but never drop control commands, persistence acknowledgments, or security-relevant events.

Do not optimize by disabling features present in the Java comparison, changing upload limits, reducing peer counts, or hiding helper-process memory. Investigate measurements before adding GPU rendering, custom allocators, lock-free containers, or another process boundary.

## Phases and Dependencies

Each phase contains multiple task cards. A task should normally deliver one testable behavior across a small number of modules, not “port this repository.” Phase gates describe integrated outcomes; lower-risk independent tasks can overlap once their contracts are fixed. Parallel agents are optional and require the active session's authorization.

| Phase | Outcome | Required integrated predecessors |
| --- | --- | --- |
| P00 | Frozen sources, expanded feature inventory, Java baselines | None |
| P01 | Architecture choices and high-risk feasibility evidence | P00 source/fixture contracts |
| P02 | Build, test, host contracts, persistence and diagnostics | P01 core decisions |
| P03 | Working headless torrent vertical slice | P02 and AD-001 |
| P04 | Usable desktop and first two plugin features | P03; UI proof from P01 |
| P05 | Standard networking and torrent-engine parity | P03; P02 network contracts |
| P06 | BiglyBT DHT, related content, ratings and public messaging | P05; P01 overlay proof |
| P07 | Tor and I2P feature parity | P05; P06 interfaces for overlay features |
| P08 | Library policy, automation and built-in services | P03, P04, P05 |
| P09 | Media, transcoding and device services | P04, P05; file-streaming contracts |
| P10 | Complete remote control and optional measurement | P05, P06, P08; applicable P07/P09 capabilities |
| P11 | Complete settings import, platform integration and updates | P04 and feature schemas from P05–P10 |
| P12 | Full parity, performance, resilience and release qualification | P06–P11 |
| P13 | Repeatable upstream maintenance | P12 release baseline; process rehearsed earlier |

The major dependency is standard torrent facilities → BiglyBT-specific distributed services → dependent plugin behavior. Mainline DHT does not substitute for BiglyBT/Azureus DHT. P08 may run alongside P06/P07; related-content search integration closes only after both relevant service contracts exist. Packaging smoke tests begin in P02, not at the end.

## P00 Source and Behavior Baselines

**Work packages:** P00-01 provenance; P00-02 feature inventory; P00-03 reference fixtures; P00-04 resource baseline.

- Resolve full source commits, repository URLs, submodules/LFS where applicable, bundled archives, transitive dependencies, and distributed artifact checksums. Reconcile the mlDHT POM's upstream `067f434` reference with the actual shipped engine; it is not automatically the cloned fork's HEAD.
- Use pristine disposable source checkouts for reference builds. The current BiglyBT checkout has a tracked service-file deletion and untracked local material. Do not restore, delete, or treat these as part of the upstream commit. Its JDK 9+ Maven profile can delete source files, so record build-induced changes separately.
- Expand every seed feature family into observable behaviors, settings, UI actions, formats, network protocols, and platform variants. Include built-in plugins, headless/console functionality, legacy integration paths, assets, and localization—not only external plugins.
- Establish sanitized fixtures and a Java oracle harness at the frozen revisions. Store source provenance and artifact hashes for expected results. Classify known bugs and nondeterministic outputs; never preserve an unsafe defect merely because the oracle exhibits it.
- Record test hardware/OS, JDK/SWT, enabled plugins, settings, data sets, and baseline CPU/memory/throughput. Bind local test services to loopback and use generated content by default. Public swarm, router configuration, or live measurement tests require explicit scope and consent.

**Exit:** all 18 source records resolve; additional required dependencies are listed rather than assumed; every discovered feature has an owner/phase or explicit open decision; baseline fixtures and performance runs can be reproduced. Inventory completeness is reviewed against source directories, menus, configuration sections, APIs, and installed feature lists.

## P01 Feasibility and Architecture

**Work packages:** P01-01 UI stress prototype; P01-02 engine comparison; P01-03 overlay/crypto interoperability; P01-04 I2P/media feasibility; P01-05 architecture decisions.

Build small disposable proofs, each with a recorded question, fixed input, measured result, and stop condition. They are not production features and must not be marked ported.

P01-01 follows the [UI qualification work order](migration/UI_QUALIFICATION.md), including fixed synthetic workloads, real display/assistive-client cases, performance evidence, and the AD-002 review. [Execution gates](migration/EXECUTION_GATES.md) assigns the remaining architecture decisions and unresolved inputs.

1. Exercise 1,000 and 10,000 synthetic torrents in a model-backed table, live peer/piece views, chart histories, sorting/filtering, and repeated open/close cycles. These are test tiers, not promises about supported capacity.
2. Reproduce cascade menus, checked/radio actions, dynamic enablement, the custom tabbed torrent popup, nested dialogs, detached views, keyboard focus, screen-edge placement, and high-DPI behavior. Test Linux Wayland/X11 for the first release; retain Windows/macOS cases for later platform qualification.
3. Run equivalent engine workloads for storage/resume, torrent versions/extensions, scheduling hooks, and statistics. Document missing BiglyBT semantics, integration cost, and memory/CPU results for AD-001.
4. Round-trip BiglyBT DHT/message fixtures through a native parser/crypto adapter. Prove required I2P stream/datagram/identity behavior; a SOCKS-only demo is insufficient. Test native video embedding and seeking, including incomplete-file streaming.

**Exit:** AD-001 through AD-006 have decisions or precisely isolated remaining questions with owners. The selected approach can satisfy critical UI, privacy, data-integrity, and overlay requirements. Any proposed scope reduction returns to the user; uncertainty does not silently become “not supported.”

## P02 Native Foundations

**Work packages:** P02-01 build/CI; P02-02 types/events; P02-03 configuration/storage; P02-04 test and benchmark harness; P02-05 capability contracts.

- Introduce reproducible [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html), dependency locks, formatting/static analysis, and the test framework selected in P01. Pin explicit GCC 16.0.1 and Clang 24.0.0 paths, exact snapshot/package identities, and verified standard-library/runtime pairs; fail clearly on an unintended compiler build. Configure separate release, debug, sanitizer, unit, integration, and UI build directories/jobs. Begin Linux compile/DEB smoke checks now; qualify other OS families after the first Linux release.
- Implement bounded parsing and serialization, byte/size/time types, clocks, cancellation, error categories, logging redaction, and resource limits. Reuse the selected engine's parsers when appropriate; do not create competing formats without a reason.
- Define command/query/event contracts for downloads, settings, categories/tags, lifecycle, file access, proxy policy, statistics, and extension capabilities. Document executor affinity and versioning. Use fake implementations to unlock UI and pilot feature tests.
- Preserve original-compatible settings and unknown fields, atomic save/recovery, shared profile locking, single-instance rules, and a read-only compatibility scanner. Version native service contracts without replacing the Java persistent schema. Use isolated generated profiles for tests. Define an updater interface but do not install updates yet.
- Provide deterministic network/clock/process fakes, fixture provenance validation, sanitizer runs, and benchmark result schemas. Make offline/local tests the default.

**Exit:** clean builds and deterministic tests pass; core links without Qt Widgets; native tests cannot open the normal Java profile; malformed fixtures are rejected within resource limits; architecture boundaries have automated checks.

Planned verification interface, to be implemented in this phase:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset unit --output-on-failure
ctest --preset integration-local --output-on-failure
ctest --preset ui --output-on-failure
```

An unavailable preset is pending work, not a successful check. Sanitizer-specific presets and a provenance-validator command must also be documented when implemented.

## P03 Headless Torrent Vertical Slice

**Work packages:** P03-01 torrent intake; P03-02 disk/session lifecycle; P03-03 controlled transfer; P03-04 resume/recovery.

Deliver add → validate → allocate/select files → connect → transfer → verify → seed → pause/resume → restart in a local test swarm. Preserve infohash calculation from the proper metadata bytes, file priorities, safe path handling, completion state, and user-visible errors. Support the torrent versions required by the P00 inventory; unsupported versions must fail explicitly during intermediate development.

Test partial files, corrupt blocks, cancellation, disk-full, permission errors, path collisions, sparse/large files, interrupted writes, and safe remove-versus-delete behavior. Implement durable resume state and reconstruct/recheck when imported data cannot be trusted. Cover magnet intake/metadata retrieval with a controlled peer fixture before public discovery is enabled.

**Exit:** the headless application completes and resumes seeded local workloads without corrupting content or unrelated files, passes fault injection, and exposes stable commands/events usable by the desktop. This is the first functional milestone, not full network parity.

## P04 Desktop and Pilot Plugin Ports

**Work packages:** P04-01 library shell; P04-02 detail views/actions; P04-03 graphs/menus; P04-04 location provider; P04-05 scheduling.

**User priority, 2026-10-18:** implement the original **classic view** first.
Use the original `az2` shell, torrent tables, tabs, menus and controls as the
behavior/layout reference before other interface modes. The current direct-folder
offline library/files preview is partial; it does not establish classic-view
parity. Follow the [classic-view task](migration/tasks/P04-01-classic-preview.md).

Implement the main window/sidebar, torrent intake/options, library and file tables, status/notifications, peer/tracker/piece details, and basic settings. Share application actions with the headless/RPC layers; widget callbacks must not become a second implementation of torrent policy.

Use [Qt model/view classes](https://doc.qt.io/qt-6/model-view-programming.html) and delegates instead of a widget per table cell. Use [QMenu](https://doc.qt.io/qt-6/qmenu.html) for ordinary cascades. Prototype the tabbed popup as a custom widget with shared actions; [QWidgetAction](https://doc.qt.io/qt-6/qwidgetaction.html) is an option, not an assumption of identical native-menu behavior on every OS.

Port `azlocprov` against a pinned native MMDB reader. Port `SpeedScheduler` logic/XML import with fake-clock tests, then attach its Qt editor and real download policies. Preserve schedule overlap rules and ownership of paused downloads. Record separate source/component baselines for the service and UI if they land at different times.

**Exit:** normal torrent workflows run through the UI; invalid/destructive actions require the intended confirmation; repeated window/menu lifecycles pass; both pilot features pass Java/native fixtures and have verified provenance records.

### UI Acceptance Checklist

Use `uis/src/com/biglybt/ui/swt/TorrentMenuFancy.java`, `views/PeersGraphicView.java`, and `components/graphics/{SpeedGraphic,MultiPlotGraphic}.java` as source references, alongside the table/view infrastructure.

- Tables: stable selection during sorting/filtering, configurable/persisted columns, multi-selection, keyboard navigation, context actions, drag/drop, and bounded memory at each data tier.
- Graphs: correct units/time scales, retained-history policy, peer hit testing/tooltips, resize/high-DPI behavior, and separation of telemetry sampling from painting. Start with custom painting and bounded ring buffers; benchmark before adding a scene graph or graphics dependency.
- Updates: batch UI deltas; avoid full-table rebuilds on each event. Suspend animation/repainting when hidden or minimized while retaining necessary backend sampling. Verify with counters/traces, not screenshots alone.
- Menus/windows: cascades, tabs, dynamic items, focus return, Escape/outside-click dismissal, pointer and keyboard paths, multi-monitor placement, stale-target handling, and detached windows.
- Cross-platform: accessibility, localization/text expansion, RTL where applicable, native shortcut conventions, clipboard, tray, dark/light themes, and display scaling. Golden images supplement behavioral tests; pixel identity with SWT is not required.

## P05 Standard Network and Engine Parity

**Work packages:** P05-01 discovery/trackers; P05-02 transports; P05-03 network policy; P05-04 engine semantics.

Integrate TCP/uTP, HTTP/UDP trackers, Mainline DHT, magnet discovery, PEX, web seeds, and other extensions established by P00. Treat the `mlDHT` wrapper and engine as one behavior set with two source records; reuse a native implementation if AD-001 selects it. Audit `azutp`'s Java translation/local fixes against the chosen native transport rather than translating inactive JNI scaffolding.

Preserve private-torrent rules, bind-address selection, IPv4/IPv6, interface/VPN changes, proxy authentication/DNS policy, IP filtering, rate limits, and reconnect behavior. Port discovered UPnP/NAT-PMP mapping and IPv6 firewall behavior with fake-router tests. Treat protocols not present in the baseline as separately approved enhancements, not assumed parity requirements.

**Exit:** controlled interoperability tests cover each protocol/extension and failure path; congestion and throughput tests pass; unwanted direct-network fallback is absent; torrents with different allowed networks remain isolated.

## P06 BiglyBT Distributed Services

**Work packages:** P06-01 overlay transport/routing; P06-02 distributed database/transfers; P06-03 message/chat services; P06-04 related content/ratings/UI.

Port the required functionality of `core/dht`, `plugin/dht`, `pifimpl/local/ddb`, `core/content`, `core/security`, and `plugin/net/buddy`. Preserve network identifiers, keys/value formats, routing/persistence, NAT-transfer behavior, identities, message signatures, ordering, duplicate handling, and quotas.

Integrate `azmsgsync` with the native chat/buddy services, `aercm` with related-content/search contracts, and `azrating` with distributed ratings and optional chat integration. Public-network parity can be completed first; I2P-dependent cases remain open until P07. Search/subscription presentation that depends on P08 cannot be marked complete prematurely.

**Exit:** Java/native interoperability and malformed-input tests pass for each covered network; public related content, chat, and rating workflows function; remaining cross-phase cases are explicit feature rows rather than hidden stubs.

## P07 Anonymous Network Integration

**Work packages:** P07-01 Tor controller; P07-02 I2P adapter; P07-03 overlay integration; P07-04 privacy/failure tests.

Port `aznettor` process/config/control/SOCKS/onion-service behavior and `azneti2phelper`'s required router/client/DHT behavior using the architecture proven in P01. Pin helper versions and artifacts independently of plugin source commits. External-router mode in the Java implementation does not itself remove Java client dependencies.

Test identity persistence, destination resolution, stream and raw/repliable datagram support, network mixing policy, proxy DHT behavior where required, suspend/resume, and helper crashes. Include DNS and packet-observation tests proving the absence of forbidden public-network traffic. Make service activation and resource limits explicit.

**Exit:** anonymity-network features interoperate with the reference, cross-network isolation holds under failure, and all P06 cases requiring I2P are closed. An unproven router-protocol feature blocks its feature row; it does not justify a silent fallback.

## P08 Library Policy and Built In Services

**Work packages:** P08-01 queue/seed policies; P08-02 tags/history; P08-03 search/subscriptions/RSS; P08-04 sharing/tracker/advanced features.

Complete categories/tags and constraints, start/stop/seeding/removal rules, speed-management behavior, schedules, download history/archive, relocation, backups, and notification/automation behavior. Test rule interactions using deterministic event traces, including user overrides.

Port metasearch, subscriptions, RSS, sharing, the embedded tracker, external seeds, and discovered content/device hooks. Explicitly inventory and cover advanced engine integrations such as `GlobalManagerFileMerger`/swarm merging and lightweight seeding; a native engine's standard feature list is not a substitute. Document any script/provider ecosystem requiring a compatibility decision.

**Exit:** each inventoried built-in service has a tested disposition, advanced workflows preserve data and policy, and settings/UI/API consumers use the same service implementation. No functionality disappears merely because it is not in an external plugin repository.

## P09 Media and Device Services

**Work packages:** P09-01 file-streaming API; P09-02 embedded player; P09-03 transcoding; P09-04 UPnP AV.

Port `azemp` controls/subtitle/streaming behavior around a pinned native media engine; replace SWT/JNA surfaces. Preserve incomplete-download playback, seek priorities, buffering, and cancellation. Test window embedding on each graphics platform selected in P01.

Port `vuzexcode` profile interpretation, process supervision, progress/errors, cancellation, and narrow NetBIOS-name functionality. Retain suitable FFmpeg/MediaInfo subprocess boundaries and validate arguments against pinned versions. Integrate `azupnpav` discovery, content directory, HTTP ranges, renderer/device behavior, and transcoding through the shared device services.

**Exit:** generated test media plays/transcodes/streams correctly; seeking never reads unverified content as valid data; slow/disconnected clients and crashed helpers leave no unbounded queues, zombies, or orphan jobs. Optional media disabled means its engines are not resident unnecessarily.

## P10 Remote Access and Measurement

**Work packages:** P10-01 local RPC; P10-02 remote/pairing security; P10-03 remaining APIs/frontends; P10-04 measurement.

Recreate `xmwebui`'s Transmission-compatible methods and BiglyBT extensions from fixtures and `rpc-spec.txt`; retain usable static web assets. Implement session IDs, authentication/authorization, read-only mode, structured errors, subscriptions/tags/search, lifecycle controls, and cancellation. Close pairing/encrypted-tunnel behaviors separately with interoperability and abuse tests. Bind the development service to loopback; publishing it externally is a separate action.

Cover existing simple APIs, console/headless operation, and legacy remote interfaces discovered in P00. Replace unsafe/obsolete exposure mechanisms only through an explicit disposition. Port `mlab` as an optional supported measurement integration with explicit user initiation, cancellation, and privacy disclosure; legacy server availability is not assumed.

**Exit:** contract tests cover all approved methods and clients, unauthorized and malformed requests fail safely, disabled capabilities return explicit errors, and remote web assets do not introduce a browser engine into the ordinary desktop process.

## P11 User Migration and Platform Delivery

**Work packages:** P11-01 complete import/export; P11-02 desktop integration; P11-03 native updater; P11-04 packages and rollback.

Early development previews replace the current `dist` after their relevant checks;
do not retain previous preview trees for rollback. Keep source history and concise
test results. Retained release builds and updater rollback qualification become
useful when there is a usable release candidate, rather than an offline sample
preview (user clarification, 2026-10-29).

Extend P02 compatibility to settings, torrent inventory, file locations, schedules, tags/categories, subscriptions, histories, identities, and plugin data. The acceptance workflow is close Java BiglyBT and open BiglyBTC against the same files, then switch back without conversion. Preserve unknown data and use recoverable backups for writes. A read-only compatibility report may diagnose unsupported or ambiguous state, but an import ceremony or new native-only profile does not satisfy the final requirement. Never open the same writable profile from Java and native clients simultaneously. Test only isolated generated/copied fixtures; do not deserialize arbitrary legacy objects as executable Java code.

Separate profiles do not make concurrent writes to the same torrent payload safe. Require exclusive ownership of shared content during migration and rollback, or use isolated copies for testing. Confirm which application may write each location before resuming transfers.

Complete file/magnet associations, tray/notifications, startup control, sleep prevention, single instance, locale/assets, permissions, and OS integrations. Replace `azupdater` with a separately testable native updater: authenticated artifacts, staged replacement, interrupted-update recovery, locked files, restart, and rollback. Updates must not modify active torrent data. Signing credentials and production releases require user-controlled authority.

Linux packages contain application files and optionally private Qt/ICU runtime
files. The smaller variant declares system Qt dependencies; both declare external
C++ runtime/engine/OS dependencies. Native plugin updates use GitHub assets selected
by signed compatibility metadata; do not infer installation from shared config
keys or overwrite Java `plugin.properties`/JARs. Test first native installation,
disabled/missing plugins, Java update/uninstall, crash rollback and switching back
against generated shared profiles. Java uninstall can remove the whole plugin
directory, including native artifacts. The native updater must tolerate this
without recreating explicitly uninstalled plugins. See AD-006.

The user accepted retirement of Java-only `dorkboxupdater` on 2026-10-17; follow
[AD-007](migration/decisions/AD-007.md). Omit its native repository/release/download
and keep native tray support as a separate requirement. Preserve original Java
files/settings; retirement never authorizes installation cleanup or a restart.

**Exit:** each supported OS installs, upgrades, uninstalls, and rolls back in isolated test environments; imports are repeatable and preserve the original profile/data; crash recovery and upgrade/downgrade boundaries are documented.

## P12 Release Qualification

**Work packages:** P12-01 parity closure; P12-02 resource tuning; P12-03 resilience/security; P12-04 upstream-sync rehearsal; P12-05 release review.

Review the complete feature matrix against P00 and all subsequent discoveries. Run long sessions, repeated window/plugin/helper lifecycle tests, large libraries, many connections, low-disk conditions, network changes, corrupt metadata, interrupted writes, and update failures. Fuzz parsers; run sanitizers and race checks separately where required by the toolchain.

Run the resource workloads below using release builds. Optimize demonstrated hot paths while preserving functionality and robustness. Produce a feature availability matrix, dependency/artifact manifest, notices, migration limitations, recovery instructions, and reproducible release evidence.

Rehearse one real upstream update for a bounded component: identify a newer commit, classify its changes, implement relevant deltas, rerun fixtures, and advance only its verified integration baseline. Also demonstrate rollback to the previous native release without losing the source-history record.

**Exit:** all in-scope features are verified; intentional differences are explicitly accepted; no release-blocking corruption, privacy, or lifecycle defects remain; resource budgets pass; packages and upstream provenance identify the exact tested implementation. A test skipped on an unavailable OS remains pending.

## P13 Upstream Maintenance

Follow [Upstream Tracking](migration/UPSTREAM_TRACKING.md). Review upstream changes before each release, prioritize security/protocol/data-integrity fixes, and record every incorporated, irrelevant, deferred, or rejected change. Do not periodically pull source branches into the port and assume Git can translate Java changes into C++.

Maintain native dependency/artifact locks separately from behavioral provenance. Replacements such as a native DHT/media library do not eliminate the need to review upstream plugin behavior. Update the feature matrix and fixtures when upstream adds a feature. The initial migration ends at the accepted P12 baseline; maintenance continues without redefining that completed release.

## Performance Plan and Proposed Gates

P00 measures the baseline; P01 freezes budgets and benchmark noise bounds before implementation tuning. The following are proposed targets, not existing results or user-approved acceptance numbers.

| Workload | Measurements and proposed gate |
| --- | --- |
| Idle desktop and minimized session, with equal features | At least 30% less process-tree resident/private memory and 30% less CPU time where the Java CPU baseline is measurably above noise. Freeze an absolute idle/wakeup budget where a ratio is meaningless. |
| Controlled transfers and hashing | No more than 5% throughput regression outside measured noise, with equal peer/rate limits, storage/cache conditions, and correctness. Report CPU per transferred/verified GiB and cache effects. |
| 1k/10k library UI tiers | Proposed p95 input-to-visible-response below 100 ms during updates; stable selection and bounded memory. Measure paint/model/sort work separately. |
| Hidden graphs and views | No animation-driven paints; necessary statistics sampling remains active at its agreed cadence. |
| Repeated create/destroy and 24-hour soak | No unbounded growth; warm-up, expected caches, retention windows, and a numerical allowed-growth budget are fixed in P01. |
| Tor/I2P/media/remote service workloads | Report each service plus whole-process-tree totals; inactive features have no unexplained polling/retained engines. |

Use at least five repeated shorter runs where practical and longer soak runs, with warm-up, medians/tails, dispersion, and raw results. Record hardware, OS, versions, build flags, fixture hash, enabled features, network/storage conditions, and settings. On Linux distinguish RSS from proportional/private memory to avoid double-counting shared libraries; use appropriate private/working-set metrics on other OSes. Do not sum unlike metrics or compare debug/sanitized native builds against release Java workloads.

If a target proves unrealistic, produce the measurements and a decision request; do not lower it after seeing a regression without recording the change. Correctness and privacy are mandatory even when a faster implementation is available.

## Agent Execution Protocol

1. **Resume from records.** Read `migration/STATE.md`, relevant feature rows, task dependencies, source locks, and local Git status. Do not repeat completed investigations or treat an old chat summary as implementation evidence.
2. **Select one ready task.** Create a task card with allowed write paths, read-only sources, exact input commits, acceptance commands, expected outputs, and non-goals. Large tasks must split at a behavior/interface boundary.
3. **Pin and characterize.** Record source files/symbols and the behavioral fixtures before porting. Resolve dependencies outside the assigned tree instead of inventing simplified substitutes.
4. **Implement a vertical slice.** Keep application semantics independent of UI/native-library details. Add tests for normal, boundary, cancellation, and failure behavior. Stubs must report unavailable capability and cannot satisfy a gate.
5. **Review and verify.** Run focused tests, integration tests affected by the change, provenance checks, and relevant benchmarks/sanitizers. Request a separate review when possible; an agent must not fabricate an independent reviewer if only one is running.
6. **Integrate and record.** Once actual implementation commits exist, store their immutable references and the source baseline per component, update feature evidence, and write a concise handoff. Do not create Git commits merely to satisfy the template when committing is outside the current authorization; leave the task implemented/uncommitted, not verified.
7. **Stop at the boundary.** Report blockers precisely: missing source, contract, test environment, decision, or authority. A blocked component does not authorize changing unrelated services or disabling its feature.

With authorized parallel agents, use separate worktrees/branches or explicit non-overlapping write scopes. One integrator owns shared headers/contracts, build/dependency files, source locks, and global state. Workers submit task-local patches and evidence; they do not concurrently edit the lock or global state. Contract changes require consumer impact review. Integrate small verified slices frequently; avoid separate long-lived “UI rewrite” and “core rewrite” branches that cannot run together.

Task status is `planned → ready → in_progress → implemented → verified`; `blocked` records the missing prerequisite and next action. `deferred`/`retired` require an accepted scope decision. A phase closes only when its feature rows and evidence satisfy the gate, not when all source files have C++ counterparts.

## Risks and Decision Triggers

| Risk | Required early evidence or response |
| --- | --- |
| Native engine cannot expose BiglyBT semantics | AD-001 gap tests; choose an adapter, scoped engine changes, or a port before dependent work expands. |
| I2P or BiglyBT overlays do not interoperate | P01 wire/identity tests; isolate missing capabilities and seek a decision instead of substituting Mainline DHT or public traffic. |
| Exact Java build inputs are missing | P00 artifact/build provenance; label the oracle unresolved and avoid unverifiable parity claims. |
| Rich UI requires excessive CPU/memory | P01 model/paint/menu benchmarks; adjust implementation within the agreed workflow before considering another UI framework. |
| Imported settings/resume data are ambiguous | Dry-run report, explicit mapping, content recheck, and preserved original profile. |
| C++ introduces ownership or parser defects | Contracted ownership, bounded parsing, fuzzing, sanitizers, and failure-injection gates from P02 onward. |
| Long migration drifts from upstream | Frozen component baselines, explicit A→B review records, and a synchronization rehearsal before release. |
| Feature scope expands silently | Add feature rows with source evidence and user-approved scope decisions; do not redefine completion opportunistically. |

## Immediate Work Queue

Start with P00-01 through P00-04. Then run the UI/engine/overlay feasibility tasks in P01, settle the architecture decisions, and only then create the production foundation. The first user-visible demonstration should be the P01 UI proof; the first functional release candidate for limited testing is P03/P04. Neither should be called the completed migration.

Historical continuation checkpoint (2026-10-18, before adapter qualification):
P06-27 had GCC/Clang10-test passes but
two leak-enabled sanitizer lanes time out on the unchanged DB CLI120s bound.
Keep it in progress with both failure capsules; no blind retry/timeout extension.
Continue P02-08 worker final cleanup, P03-05 reusable engine owner and P06-29
ratings queue. P03 source references freeze14,068 export/pause plus2,280
completion observations; native completion release/promotion remains gated.
The running original program and its profile remain untouched.

Later continuation: P06-29 bounded rating publication policy passes three fresh
compiler/sanitizer lanes (653 source comparisons,386 ownership/capacity assertions
and112 CLI observations per lane). Actual DDB/timer/plugin-host activation stays
pending. P02-08 passed GCC/Clang/ASAN in V3 but its TSan allocation-test link
conflict remains preserved; reviewed V4 uses the real sanitizer allocator through
test-only linker wrapping and now passes all four fresh roots. P03-05's frozen
seed-owner review found three blockers (entry-cap test branch, deadline ordering,
advertised parent identity); fix them in a versioned draft before execution.
P06-28 Mapping has an independently reviewed native draft and a qualification
runner under preparation. Measure the separately reviewed DB stdio adapter
candidate before considering another P27 qualification; retain all original
deadlines and record any accepted one-file provenance override explicitly.

Latest continuation: the independently reviewed stdio adapter now qualifies the
bounded router-contact record in three fresh ten-test lanes, with346 actual-Git
input mappings and prior failed attempts preserved. Mapping qualification is
active; seed retryV4/Host integration, fourlane expiry qualification and partial
resume source collection are the next serialized execution gates. Keep all old
fixtures/deadlines and running Java isolation. A bounded pass is a checkpoint;
the full migration, seamless client switching and measured performance remain
required by the original plan.

Qualified checkpoint (2026-10-18): bounded P06-28 mapping passes three fresh
ten-test lanes and499 actual input Git-blob mappings at implementation
88ffa2eeb782bf1f775689d4f078af469325a188. Original2,881 source-only rows remain
excluded, and wider source integration remains null. Both revised cleanup proofs
passed; partial-resume source collection is active. Next execution gates remain
seed retry/Host, corrected four-lane expiry and storage-retirement qualification;
resume-history helper authoring proceeds separately. Preserve all failures and
the running original Java program. Full migration and seamless switching remain
unfinished; a bounded checkpoint is not permission to stop autonomous work.

P03-06 source checkpoint: exact partial-checkpoint helper collected1,258scenarios/7,923commands and60predeclared faults; independent raw-row/process/copy review passed. Main fixture manifest6d8ad1e48f668ba3feb134d76ae6cb24edbe40a1dcd091ea318aed607c15c45c freezes98copied files. Native partial checkpoint and actual storage/history remain pending; P03-05 task stays unchanged as a helper input. RetryV4 orchestration is explicitly adopted after22processproof cases passed; approval-bound inspection passes, but native seed tests have not begun.

Continuation at 03:00 UTC: P03 retryV4 is active in its serialized native slot.
Its GCC build snapshot was fsynced before runtime-only reconfiguration; the
normal build observed no compile/link steps and all26 prior binary bytes stayed
identical. Metadata checks precede fresh lower/owner tests; no lane pass yet.
The partial-checkpoint API proposal freezes exactly2,416 original-plan
projections and5,507 excluded rows before C++ implementation. Independent review
is pending; no partial/history/storage authority follows from this proposal.

Resume-history helper V1 remains blocked by its log acquisition/close error
ordering; V2 preserves that failure and passes source review. Its new12-label
Python-only proof passes source review/read-only inspection and waits for an
idle native/compiler/JVM slot. This helper pins the current pre-adoption native
inputs: finish its source collection before adopting changed seed inputs, or
create an explicit versioned compatibility amendment and review it first.

Storage retirement's pure model passes source review but remains uncompiled.
Its qualifier V1 has two metadata error-preservation blockers: fsync failures
can be replaced by close errors, and metadata failure recording/propagation can
be replaced by the first SIGTERM. Preserve V1 and fix those in a separately
reviewed V2 before the18-label cleanup proof or four-lane model qualification.
The expiry15-label proof is reviewed/approved for later idle-slot execution;
actual native qualification still waits for that proof and its result review.

The concrete libtorrent storage audit is source-only. Its qualified2.0.12 root
is `/tmp/biglybtc-deps/libtorrent-2.0.12-release/libtorrent-2.0.12`, verified
against all444 existing source hashes. The earlier supplied2.0.11 path was
rejected before execution, with the mismatch retained. Construction identity,
buffer/observer/deferred-tick ownership, native destruction and error-erasing
release callbacks need an explicit bridge contract; cleanup leases must avoid
the sealed-plus-zero ordinary-job circular wait. No derivative or physical
release/durability claim has been implemented. Continue the independent chapters
while those gates remain open; the current queue and exact frozen hashes are in
`migration/recovery/20261018/execution-queue.json` and `active-continuation.json`.

Continuation at 03:45 UTC: GCC retryV4 passed all25 retained tests and34 owner cases; Clang build passed and metadata checks continue. No full three-lane completion or seed adoption yet. The final evidence validator is frozen for review and will require the same native commit to contain all365 mapped inputs plus exactly7 promoted native targets. Finish the history helper before changing its pinned old seed source; main build wiring requires separate review and qualification.

The bounded bencode capability is now a frozen, uncompiled C++23 draft with32 cases, beside the unchanged core codec. Its bounded memory method passed independent source review; implementation review and actual qualification remain pending. Java encoder source audit is frozen separately, with no new byte-equality credit. Bridge composition V1 blockers are preserved and a V2 repair awaits review; storage qualifier V2 passes metadata source review and its18-label process proof awaits independent review and idle execution. Frozen sources are copied and fsynced in migration/recovery/20261018/frozen-work-0345. Continue autonomous work until explicit user return.

Continuation at 04:00 UTC: bounded codec V2 passes independent source review after focused additions for actual hard input/node/value/key limits, writer depth/second-root and realloc coverage. It remains uncompiled. P02-09 records the task; a separate source qualifier is being authored. Original261 historical inputs are copied verbatim into explicit context images before later engine changes; actual compiled inputs must still match current literal paths. The direct unchanged Java BEncoder helper is source authoring only.

Storage18proof passes independent/full root source review and a10.50-second read-only inspection; actual signal proof remains queued after history/expiry and before native model qualification. Bridge V2 design passes review and fake code is being authored; unexpected network-worker shutdown with live retained buffer owners is a recorded technical blocker, without a new fallback protocol. The337-file reviewed-work-0400 capsule preserves frozen source/reviews and exact historical context, excluding all live native evidence. Continue until explicit user return.

Continuation at 04:10 UTC: direct BEncoder helper source is frozen with1164 predeclared normal cases, zero allowed source errors and no expected encoding bytes; source review and its own lifecycle proof precede all javac/JVM execution. Tail-counterfactual design8437 freezes216 synthetic dataflow cases and awaits independent review. It proposes complete original map/checker/hasher bodies with inert providers; all-zero required-hash tokens are only dispatch inputs and grant no verified-piece or full-manager causal credit. The old pure-v2 original-only8/9 result remains unresolved. Clang fresh retained tests are active; allmetadata passed and its exact owned CTest was directly observed untraced.

Continuation at 04:30 UTC: GCC and Clang retryV4 both passed all25 retained
tests and34 persistent-session owner cases. The sanitizer lane is building;
three-lane completion and native adoption remain pending. Both actual Clang
CTest leaders were directly observed with TracerPid0 and exact owned identities.

The direct BEncoder helper passes independent source review6cc539; all1164
normal DTO commands, original source/core bindings and historical images were
checked without executing the helper. Its own22-label process-proof source is
being authored. The bounded codec qualifier is frozenb457 and under full root
source review. Actual partial-checkpoint C++ authoring34b and fake bridge
authoringda9 continue independently; neither has compiled or run. Preserve all
these distinct gates rather than treating source review as execution evidence.

Tail design8437 passes independent source reviewf0ae for direct-method
synthetic dataflow only. Before helper authoring, explicitly preserve all216
cases and their pre-observation whole-mapper path labels:86 reach the last-piece
builder,124 use the general one-piece loop and6 use the single-file fast path.
No helper, causal fix or verified hash result follows from that design review.
The reviewed-work-0430 capsule preserves the closed source/reviews and original
context images; mutable agent drafts and live qualification evidence are
excluded. Continue autonomous work until explicit user return.

Continuation at 04:50 UTC: the sanitizer build passed; actual binary linkage
and metadata checks precede its fresh retained/owner tests. GCC and Clang remain
fully passed. The final three-lane gate is still open and native seed adoption
still waits for the source-history dependency.

Codec qualifier b457 passes full independent root source review56a755; its own
18-label proof is being frozen. Direct encoder helper own22-proof1a544 passes
independent full source review6062 and read-only hash/AST inspection in14.63s.
Its actual mock run remains unexecuted;100full parent guard passes motivate a
pre-attempt2400s outer budget plus60s termination grace, without changing any
subject deadline. Conditional be555 deliberately fails its actual status gate;
root must release a new approval when the serialized slot is idle.

Tail design V2b546 passes independent review1446. Its exact216cases and their
86/124/6source-inferred path labels remain unchanged; strict nominal callback,
buffer, monitor and semaphore requirements apply before helper freeze. Source
authoring is approved separately6528, with no digest expectation, native fix or
whole-manager causal result. Frozen proof/design/reviews persist in the0450
recovery capsule; mutable C++/proof/helper work stays separate. Keep progressing
independent chapters and preserve unresolved gaps until the user returns.

Continuation at 05:00 UTC: bridge C++V1 is frozen34d156 with40declared files,
18component files and all720original guards unchanged. Its16translation units
and72new fake cases plus40retained model cases remain uncompiled. Independent
cross-review follows the partial-policy freeze. Unsupported permanent N-worker
loss with live retained effects is explicit; narrower supported shutdown tests
do not substitute for that recovery scenario.

Codec own18proof670318 passes independent source review127eb0 and read-only
inspection in19.26s. Its72full guard-pass planning proxy motivates2400s outer
plus60s grace before any mock attempt. Corrected conditional5910 is still
deliberately invalid; original01ed pending schema is retained, with no execution
attempt. A fresh root release using the actual codec gate fields remains needed.

Partial-policy source accounting must charge both Writer and expected<Writer>
while they coexist. Frozen private Scanner layout cannot be inferred as a
portable numeric sizeof from its fields; any named reserve needs an actual
layout-metadata gate for each distinct future compiler/ABI. Source-only audit
continues without changing the frozen codec/API/budget. Logical fixed-object
charges grant no compiler-stack or RSS claim.

The0500capsule preserves677closed artifacts/18,011,259bytes, including exact
images for720bridge source origins (605unique contents). They cover temporary
dependency-source locations for reboot recovery without approving historical
image substitution for current compiled inputs. ASAN binary metadata checks
continue; its fresh retained/owner tests and full three-lane gate remain open.

Partial-policy review follow-up: frozen a10bf has an unexecuted replay-wrapper
metadata blocker. Its save()/write_json() implicit file close can replace a
write/fsync E1 with close E2 before the ledger records the original. Preserve
the frozen draft, finish the full independent review, then repair in a separate
version before any proof/replay/qualification. No native policy change is
implied. Tail helper d919 and bridge34d156 undergo independent reviews in
parallel; source-only checks give no new execution credit.

Bridge review follow-up: frozen34d156 has a source coverage gap under C09 and
P02/P03. Their original actions require a throwing delegate; current Fault
branches return typed failures. Only callback Action::Throw currently throws.
Retain the original72requirements and frozen draft; complete review and prefer
a separate actual closed throw/catch/unwind repair. Give these paths no
exception-unwind credit without it. ASAN retained CTest started05:13:09; its
exact owned leader260309 was directly observed untraced05:13:25.

Continuation at 05:25 UTC: all three source reviews are frozen. Partial V1
a10 is blocked only on PC-Q1 metadata first-error/first-signal handling; tail
helper d919 is blocked on TH1 binding collection to the exact independently
reviewed first reference/class map. Bridge34d is blocked on SB1 actual delegate
throw/unwind and SB2 a buffer-free-caused task retaining its parent. Independent
capacity and valid backward/targeted-time observations also need assertions.
Separate V2 repairs preserve each frozen V1 and all original IDs/data/bounds.
The0525 capsule fsyncs187closed files/4,291,287bytes; active repairs and live
ASAN evidence are excluded. No new native/Java/process-proof pass is claimed.
Continue the serialized execution queue and independent source chapters until
the user explicitly returns.

Continuation at 05:46 UTC: P03 retryV4 completed all three native lanes and
manager58639 exited0. Exact matrix42f75e records102 generated owner cases and
25 retained tests per lane; no semantic/whole-client credit is added. V1 final
validator stopped on its57-file pre-result runtime snapshot versus the actual
58-file closure: only ASAN libresolv.so.2, recorded by17 raw linkage checks
before tests. V2 preserves the original plan and accepts only that exact
path/hash addition. Independent review also found a direct-loader row grammar
gap; preserve V1/failure/V2 and repair only after full review. No seed adoption
is allowed yet.

History Python process proof passed12labels/33leaves, with independent review
of516raw case artifacts. First actual isolated history helper compilation is
now root-owned46322; actual class map must be reviewed before collection, then
the old-local-source dependency can close. Partial metadata V2afb9 passes root
source review5c8667; its own wrapper proof remains source work. Tail helper
V2d628 is frozen with strict reference-review/output collection bindings; no
helper/proof run is claimed. Continue independent chapters until user return.

History execution follow-up at05:52UTC: first compilation produced24actual
classes and passed independent reference reviewca544. Collection19044failed
in malformed fixture construction: list(null) supplies a null varargs array
to Arrays.asList before the original subject can execute. Preserve V2 classes,
raw groups, failure and HOME final capture; do not add a permitted source error
or claim the full6805 corpus passed. A separate V3 fixture-renderer repair is
underway with all original methods, scenarios and30fault slots unchanged.
The native7 adoption dependency remains open. Continue the independent expiry
15-case Python cleanup proof while history source is repaired. Bridge V2dc342
and postvalidator V31b7e are frozen source work awaiting independent review.

User returned for status and questions. The autonomous until-return interval
ended; all three workers stopped with durable source/report snapshots. No
compiler, JVM, native fixture or process proof remains active. Expiry15proof
passed (d3bbda) and awaits actual-result review. P03 native3lane matrix passed;
postvalidator V4ecb041 remains source-only awaiting root review/validation and
seven native files remain unadopted. HistoryV3 fixture repair0bc1 passes
independent source reviewe2ea; its new12proof has not been authored or run.
Partial45proof814 is frozen source only. Tail proofc81 is explicitly incomplete
and cannot pass its gate. Full plugin/client compatibility, performance, binary
release and active CI remain unfinished. Five human questions stay in QUESTIONS.

## Desktop preview and hosted CI decisions — 2026-10-18

The user answered the Linux/CI/review questions: amd64 Ubuntu26.04 first,
corresponding Mint/Debian qualification later, JDK17 accepted, standard
GitHub-hosted runners, and self-signed development releases. Subsequent steering
prioritizes running directly from `dist` and defers more DEB/release work.
`~/.bitlybtc` is allowed for temporary development profiles, with independent
random ports; final `~/.biglybt` compatibility is unchanged.

The first offline sample preview is built and running on the actual Wayland
desktop from `dist/BiglyBTC-preview`, with private Qt6.12 and an extracted
libtorrent runtime. It owns no TCP/UDP sockets. Twenty-two unit checks and the
generated-library/files Qt smoke test pass; real desktop interaction feedback
is pending. A20MB local DEB and user-only install had already been produced
before the direct-folder clarification; no public/signed release is claimed.
See [launch/checklist](migration/PREVIEW.md) and [precise evidence](migration/evidence/20261018-preview/verification.json).
The current dist launcher has a later path-validation fix and is distinguished
from that older local package in the independent source review. Full client and
plugin integration, live-profile switching and equivalent-workload performance
remain unfinished. Hosted-plugin CI progress is recorded separately in the
repository rollout; passing bounded plugin tests does not close those gates.

## First classic preview milestone — 2026-10-18

The first bounded classic shell now provides the Library tab, incomplete/complete
split tables, literal name filtering and shared General/Files details. All23
native unit tests pass, including25 classic Qt rows, and the generated-profile
smoke test passes at100/150/200% scale. Broader classic/client parity remains open.
The user requested milestone commits/pushes and a durable sibling project; the
clean checkout is now `~/Java/BiglyBTC`. Native source export, independent build
and publication receipts are recorded with the classic evidence. The original
Java checkout, live client/profile, HEAD and staging index remain preserved.

Public source commit: `b4f6c11a9298a3c4a250af5d40e9dc8b13efa715`.
The documentation follow-up `d2813554e4b625ad5f2b420dbdec1aacf43ff74e` is tagged
[`milestone-classic-preview-20261018`](https://github.com/d00rsfan/BiglyBTC/tree/milestone-classic-preview-20261018).
The actual GitHub-hosted desktop build passed all 23 native checks and generated
preview smoke; the existing SDK check also passed. No binary release was made.
The executable is `~/Java/BiglyBTC/dist/BiglyBTC-preview`; source/build/dependency
locations are documented in [the workspace record](migration/WORKSPACE.md).
The private implementation/export hashes and hosted reports are retained in
[the publication receipt](migration/evidence/20261018-classic/publication.json).

## Autonomous classic navigation continuation — 2026-10-18

The user is away for several hours and explicitly requested continued autonomous
work. Highest current priority is **View > My Torrents / Ctrl+1**, their favorite
screen, followed by recognizable original classic navigation and useful screens.
The original classic title is My Torrents, so the first preview's modern Library
label is being corrected. Follow
[P04-03](migration/tasks/P04-03-classic-navigation.md), keep questions in
`migration/QUESTIONS.md`, preserve the running Java client and live profile, and
publish validated source milestones from the durable sibling checkout. Work
continues across bounded milestones until the user reports returning.

Implemented/pushed My Torrents navigation:
`9abef333ab0a4178bc68e098a038d04514a37ddc`;24 native units/13 navigation Qt rows
and actual generated-profile smoke pass locally and on GitHub-hosted desktop CI.
Next implemented/pushed column controls:
`7458daa2bf576e872282166e296373c1454504f7`;25 native units/13 column Qt rows and
100/150/200% preview smoke pass locally. Hosted status is retained separately,
never inferred from the local result. `~/Java/BiglyBTC/dist` is updated; older
preview folders and any running windows are preserved.
[P04-04](migration/tasks/P04-04-classic-columns.md) records bounded scope;
[P04-05](migration/tasks/P04-05-saved-service-views.md) now advances pure saved
hosting/sharing projections toward real read-only My Tracker/My Shares data.
No running Java profile, tracker service, filesystem scan or live network is
used. Broader classic/client compatibility and performance gates remain open.


Saved-service readers are now pushed as native
`3d4f86d3f891675238be05ebbad8202c99cd6817`:27 native units with19 projection
cases/422 assertions and44 held-profile adapter cases/4,118 assertions. Both
reader suites pass GCC16 Release, Clang24 Release and GCC16 ASan/UBSan/LSan.
Standalone request-tagged Qt models/pages are pushed as
`d5ba39d326b3f08d9f77c00f4446ecf3102a5f83`:28 units and20 page Qt rows.
Both commits also pass actual GitHub-hosted desktop and SDK checks; downloaded
reports are retained in `migration/evidence/20261018-saved-services`.
Original23-file source hashes/blobs were independently rechecked unchanged.
These are source-derived synthetic fixtures, without Java-runtime equality credit.
My Tracker/My Shares Window/Bridge integration is active, with17 actual new
Bridge Qt rows passed before menu integration; no full service activation claim.


Classic saved-data navigation is now implemented/pushed in native
`0b70eb6b0565f2a1debf2e7cd321e9bcb17b31b6`: My Tracker/Ctrl+2 and My Shares/Ctrl+3
open read-only closable tabs; My Torrents/Ctrl+1 stays permanent. Current-worker
reads preserve original-format input bytes and recovery provenance without
starting services or following stored paths. One active/latest pending generation
prevents late responses from retargeting reopened tabs. Hidden torrent actions
have no target. All30 native units and17 Bridge/19 navigation Qt rows pass;
both new suites pass leak-enabled ASan/UBSan, and full three-view smoke passes
100/150/200%. Dist is updated with all103 files verified and82 identical runtime
files, preserving its predecessor. Hosted SDK passed; desktop CI is running.
The exact integration receipt records later CI observation separately. No full
service, Java-runtime equality, seamless profile switch or performance claim.


Hosted saved-view desktop run39114203091 completed successfully:30 native units
and the real generated-profile three-view smoke/captures pass. SDK run39114203106
also passed. Public milestone commit `2c309fe11dcecd4606bd848c9f780fd062391767`
is tagged `milestone-classic-saved-views-20261018`; its source is the separately
pinned `0b70eb6b0565f2a1debf2e7cd321e9bcb17b31b6`. Downloaded hosted reports are
retained with the integrated verification evidence. Earlier “running” statements
above record the previous observation and are superseded by this result.


Saved queue-position source `ec56329fdfab1a45a8eafd7bacc97c163d5815c2` is pushed,
with public verification record `a5633ee56f68ff8d031cd7bf2922d0508a55938a`.
All31 native units,17 new Qt rows and the new leak-enabled ASan/UBSan suite pass;
three-view smoke passes100/150/200%. Hosted desktop39115097757/SDK39115097703
also pass, with downloaded evidence retained. Dist is promoted without signaling
any process, and its full predecessor is preserved. Raw saved position is not
live queue Rank; DND-aware Size/Remaining/Done remain explicit OPEN prerequisites.

A corrected full source trace now establishes original az2 main My Torrents IS
closeable: TabbedEntry overrides the factory false flag for main MDI. Earlier
noncloseable-original inference is superseded by the immutable corrective report
`migration/evidence/20261018-navigation/classic-closeability-correction.json`.
[P04-07](migration/tasks/P04-07-classic-close-tab.md) implements File Close Tab/
Ctrl+W for all three known views, retained-screen Ctrl+1 reopening and one-time
saved-view width defaults. Source work/tests are in progress, not yet executed
or included in dist. Full plugin-veto/destroy-recreate/persisted-view/window/tray
semantics stay OPEN. No existing source/equality/qualification gate is removed.


Classic Close Tab source `4bfa2d85c9154ac3ede33c8298d2d7209d6589c5` is verified,
pushed and staged in dist. Public metadata `efdf27688594ceecd6d27f8bc2c6e7c0e4c5ce4c`
is tagged `milestone-classic-tabs-20261018`. All32 local and hosted native units
pass with zero skips;16 Close Tab Qt rows and three leak-enabled sanitized
lifecycle suites pass. Actual three-view/close-all/Ctrl+1 restoration smoke passes
100/150/200%. Current tabs close viaCtrl+W and My Torrents reopens with retained
screen state. Original main-TabbedEntry override is corrected by20 pinned source
files; native retained widgets/Qt neighboring-tab selection and unimplemented
plugin-veto/MRU/persistence/tray behavior remain explicit adaptations/OPEN work.
The current dist has103 verified files and82 identical runtime dependencies;
its full predecessor is preserved. No process was signaled or live profile opened.

Next focused classic prerequisite: pure saved DND aggregate metrics from already
loaded active/cache.dat attributes, without per-torrent startup IO. Independent
source math review `migration/evidence/20261018-navigation/saved-dnd-math-review.json`
pins19 files and44 complete methods. Crucial defaults are skipfss−1 (requires
original recovery, so unknown in this pure stage) and missing skipbdl0 with source
provenance. Keep selected metrics, cached controller candidates and public assumed
completion/pane policy separate. Full DND UI/classification/fallback/Java-runtime
parity remains OPEN. Minimal typed API/strict invalid-data policy is being frozen
before separately owned production and independent fixture work.


Pure saved-DND aggregate source `aaa7a1f4a84b66c0edc3d900c701e38a81fcfd2b` is
committed/pushed. All33 local units pass with zero skips;61 new cases/464
assertions pass GCC16 Release, Clang24 Release and leak-enabled GCC16 Debug
ASan/UBSan. [P04-08](migration/tasks/P04-08-saved-dnd-aggregates.md) and
`migration/evidence/20261018-saved-dnd/verification.json` record the narrow
source-derived scope. No UI wiring, Java runtime equality, completion-pane
classification or original file recovery credit. Exact-source-body Java probing
and labelled saved-selected column integration are proceeding independently.
Dist remains the existing validated Close Tab preview until a visible milestone.

Hosted desktop39118014131/SDK39118014174 now pass for the pureDND sourceaaa7,
including33units/zero skips and actual offline three-view smoke. Public provenance
`9e890be4c6096934ab27c786ef37f3f706d67f94` pins the bounded results.
[P04-09](migration/tasks/P04-09-saved-selected-columns.md) freezes the next
optional-column integration; live/full classic column parity remains OPEN.


Optional saved-selected columns are implemented/pushed in
`03e31e5ce260e1327e63cca63d22d8d5984ceae9`, with public provenance
`0e96f6a5d8f6922791ab0655dc93b13e163366af` and tag
`milestone-classic-saved-selected-20261018`. All34 units/26newQtrows pass locally
and hosted (desktop39119435708/SDK39119435750), new26Qtrows pass leak-enabled
sanitisers, and actual three-view/chooser smoke passes100/150/200%. Dist has103
verified files/82 unchanged runtime files, full predecessor preserved, no process
signaled. [P04-09](migration/tasks/P04-09-saved-selected-columns.md) preserves
strictly saved/current-cache scope and full pane/runtime/config-layout limitations.

Later exact-source-body probing matched869 selected scalars against the unchanged
actual aaa7 core archive, independently reconstructed from raw observations. The
source/body/stub distinctions,555 pre-Java mappings versus314 later independent
field mappings and six zero-credit excluded categories remain explicit. Full
manager/runtime/pane/disk/recovery parity still gets zero credit. All raw helper,
compiled classes, actual core archive, transport driver and outputs are retained
under `migration/evidence/20261018-saved-dnd` with complete hashes.

All nine public project checkouts are now durable siblings under ~/Java; complete
prior temporary plugin checkouts remain preserved. Registry validation passes18
sources/65partial components without advancing unqualified integration baselines.
Next: projection-only original classic column layout reader, ahead of any GUI
applier/config writes. Audit pins28original files/60definitions, including the
actual [visible,position,width] order despite a stale source comment, separate
TableDefault provenance, ordered multisort and opaque plugin/tail preservation.

P04-13 now persists both panes’13 native classic column choices explicitly,
under BiglyBTC.ClassicColumns.v1 in existing tables.config. Source commit
`ba03f2c6a080f856c0d8808b2b4239846dd27733` is public. Original Java table/plugin
records remain byte-exact through native writes; private startup/Reload precedence,
Primary byte-conflict checks, newer-edit acknowledgements and actual post-save
cancellation results are qualified on generated profiles. GCC42 units, Clang70
codec/storage cases,7 leak-enabled sanitizer suites and3 actual smoke scales pass;
hosted desktop39130607531 independently confirms42 units/39 new Qt rows/4 smoke
rows, and SDK39130607432 passes. The updated portable dist retains a rollback and
unchanged82 private runtime files. Three guarded original Java codec-only map
roundtrips pass with interning disabled; no manager/full-save/runtime client parity
is claimed. Java Reset All Tables erases the private map. Shared canonical layouts,
missing/recovery file creation, normal-profile switching and full client/plugins
remain open. See P04-13 and its local/hosted/source/promotion evidence.

User priority continuation (2026-10-18): after P04-14 first-save qualification,
implement Tools > Statistics with Ctrl+5 as the next classic tab. Use original
source behavior, real available measurements and explicit unavailable states;
do not fabricate live transfer or lifetime history counters.

P04-14 is qualified and published at source8efe56e4c0a4c46b8cdcb51035364db938494bb5,
metadata8127336eada41e5795d6fdadaa42eea0ff555ec1/tag
milestone-classic-layout-first-save-20261018.44 native units,195 focused Clang
cases,5 leak-enabled sanitizer suites/21 Qt rows,3scale actual smoke and all3
real-write-fault/recovery lanes pass; hosted desktop/SDK independently pass.
Dist103 hashes/82 unchanged runtimes and exact install-RUNPATH proof verify.
P04-15 now implements user-priority Tools > Statistics / Ctrl+5 from the frozen
original source audit; full original global/session/history remains open.

P04-15 first Statistics preview is qualified and published at source
`be9ad3d5f409947445b6c253b17430a9df0f6655`, metadata
`e9eef6cbf633207ae4b44691147e5ece4f5b03be`, annotated tag
`milestone-classic-statistics-20261018`. Tools/Ctrl+5 reuses one closeable tab;
Ctrl+1 preserves My Torrents state. Activity retains600 actual scoped payload
observations; Transfers reads13 original saved overall fields without writes.
GCC48 local/hosted units, Clang86 core/reader cases,9 leak-enabled sanitizer
suites,55 Statistics Qt rows and actual three-scale6-row/6-capture smoke pass.
Hosted desktop39140391574 and SDK39140391247 pass on that exact source.
Dist103 hashes verify,82 private runtime files unchanged, full prior dist retained.
Nine independent reviews cover the layers and the old fake-worker fixture
correction; all original assertions and deadlines remain. Global data+protocol
sampling, session counters, history and other original Statistics subviews need
owned persistent-session integration. They remain open with full client/plugins,
normal-profile switching and performance qualification. See P04-15 and its evidence.

<!-- p04-16-engine-statistics-completion -->
P04-16 bounded measured-engine statistics are qualified and published.
Source `5fc34b588713112b126f3cebdfc891020301a281`, metadata `30569e2631427003a4a92e7edee3572a3b34e740`,
annotated tag `milestone-engine-statistics-20261018`. All 28 changed/new source files match tested Git blobs.
GCC 50 unit suites, Clang 3 focused suites and 9 leak-enabled ASan/UBSan suites
pass without skips. New arithmetic: 22 cases, 3,719 assertions and 512 independent
rational vectors; Statistics Page: 92 Qt rows; actual owned-engine/Window/worker:
12 Qt rows. All 48 previous unit suites remain in the GCC cohort.
At each 100/150/200% scale, six offline smoke rows and four real-engine Qt rows
pass, with six offline plus two actual engine captures (24 three-scale PNGs).
Hosted desktop 39144813366 and SDK 39144813345 pass on the exact source; the inspected
desktop artifact has 50 units and eight captures. Dist 103 hashes verify,
82 private runtime files are unchanged and the previous complete dist is retained.
No original profile/payload access or process signals; original HEAD/index unchanged.

Both existing experimental engine paths sample named libtorrent2.0.12 metrics via
their existing alert consumer and owned steady time; no second consumer/thread is
introduced. Optional telemetry cannot change transfer success. The GUI binds
session/operation/sequence/target and retains only the last actual observation.
This is one measured temporary libtorrent engine session per operation,
not application-wide global/session/lifetime/history parity. Ended counters are
the last observation, not exhaustive teardown totals. No Java ten-second smoothing,
IP/tracker/DHT, history, statistics writes/reset/mark, cross-run accumulation,
persistent-owner/full-client/plugin, seamless-switching or performance claim.
Full application/global/history work still depends on unfinished P03 ownership.
This closes only the requested single milestone; further implementation is not
automatically started. Rollback: `~/Java/BiglyBTC/build/statistics-engine-20261018/previous-dist`. P04-15 and earlier
milestone evidence remain historical and unchanged. See P04-16 and
`migration/evidence/20261018-statistics-engine/` for actual source and results.
<!-- /p04-16-engine-statistics-completion -->

<!-- p04-17-engine-activity-completion -->
P04-17 bounded measured-engine traffic graphs are qualified and published.
Source `a84e4ecc6f78112c115c182aefc13bc4a1f3ac17`, metadata `9d6023fca3b322dfbe5d92d1666b7f2120cd7752`,
annotated tag `milestone-engine-activity-20261019`. All 17 changed/new source files match tested Git blobs.
GCC 53 unit suites, Clang 5 focused suites and 9 leak-enabled ASan/UBSan suites
pass without skips; all 50 earlier units remain in the GCC cohort. Existing Page
92 and owned-engine 12 Qt rows remain intact. New History: 52 Qt rows; View/Page:
26 Qt rows; real graph Window/worker: 7 Qt rows. At each 100/150/200% scale,
six offline smoke rows, four real engine-counter rows and four actual graph rows
pass, producing six offline + two counter + two graph PNGs (30 across three scales).
Hosted desktop 39167136970 and SDK 39167136902 pass on the exact source; the inspected
artifact has 53 units and ten captures. Dist 103 hashes verify,82 private runtime
files are unchanged and the previous complete dist is retained.
No original profile/payload access or process signals; original HEAD/index unchanged.

Existing P04-16 sampling is unchanged: no new alert consumer, engine, clock read,
sampling timer or thread is added. Two scoped charts consume six actual interval
rate fields. Window retains at most600 known-rate points across Page closure, with
explicit gaps and operation/profile resets; default payload Activity is preserved.
These are actual interval-rate observations from one experimental engine
operation/session, not original global Activity or application-wide/session/lifetime
history parity. Window retains at most600 known-rate points; unknown/skipped-sequence
intervals remain disconnected, known zero is real, repeated terminal samples are
deduplicated and a later actual terminal point is standalone. Ended is last observed,
not exhaustive final accounting. No Java ten-second/eleven-entry smoothing, IP/
tracker/DHT, persistent history, statistics writes/reset/mark, cross-run accumulation,
persistent-owner/full-client/plugin, seamless-switching or performance claim.
Full application/global/persistent-history work still depends on unfinished P03 ownership.
This closes only the requested single milestone; further implementation is not
automatically started. Rollback: `~/Java/BiglyBTC/build/engine-activity-20261018/previous-dist`. P04-16 and earlier
milestone evidence and their rollback directories remain historical and unchanged. See P04-17 and
`migration/evidence/20261018-engine-activity/` for actual source and results.
<!-- /p04-17-engine-activity-completion -->

<!-- p04-18-saved-history-completion -->
P04-18 read-only saved UTC daily transfer history is qualified and published.
Source `9db774659334daa2dbabd81762c819c108eb43b2`, metadata `fe09d1c9430a5a3c684d34be97a6661d08704c72`, annotated tag `milestone-saved-history-20261019`.
All 48 changed/new source files match the actual tested Git source map.
GCC 57 unit suites, Clang 8 focused suites and 11 leak-enabled ASan/UBSan suites
pass without skips; all 53 previous GCC units remain. The new projection's 21
cases and reader's 11 families pass. The 17 fixtures compare 425 original Java
day/hour queries, covering 2550 signed scalar results. The new History UI's 34
and actual Window/worker's 12 Qt rows pass; the retained Page's 92, Statistics
worker's 20, engine worker's 12 and graph worker's 7 rows also pass. At each scale
(100%, 150% and 200%), seven offline smoke rows, four actual engine rows and
four actual graph rows pass. Each scale produces 12 PNGs, including ready and
missing saved History, for 36 PNGs overall. Hosted desktop 39172584835 and SDK 39172584837
pass on the exact source; the independently inspected hosted artifact has 57
units and 12 captures. The dist's 103 file hashes verify; 82 private runtime
files are unchanged, and a complete
previous dist remains available. Original Java HEAD/index and live profile are
untouched; no original process was signaled. Offscreen captures give no manual
compositor acceptance credit.

The browser reads one fixed daily file through the held profile, exposes date
navigation and Refresh, plus 24 UTC hourly six-counter rows, and preserves missing versus
observed zero. The complete original committed Java reader was compiled in an
isolated Java 17 oracle; only its 425 valid day/hour-query totals (2550 scalars)
qualify equality. No native expected parser was copied into that oracle.
This implements only the read-only saved UTC daily traffic browser:
date navigation and Refresh, observed daily payload totals and 24 hourly rows for six
signed counters. Actual baselines are ignored; positive anchors, reconstructed
minutes, duplicate records and signed adjustments retain valid-source accounting.
Unknown or missing hours remain distinct from observed zero. Version, record
shape, input-limit and overflow handling are deliberately stricter than Java;
those errors and presence
metadata have zero Java equality credit. Native full-day projection does not
apply the original reader's current-clock end clamp. This is not the rolling
hour/week/month/year/all-time graph, cache service, live history writer, global
history/engine ownership, full-client/plugin, seamless-switching or performance
parity. No history/configuration/payload writes, scans, cache.dat access, extra
lease, engine, timer or network activity are introduced by this reader/view.
Existing saved totals, payload Activity and bounded measured-engine graphs remain
separate. Broad original first_ported/integrated source-coverage fields stay null;
only statistics.saved_history_day is implemented. The complete migration is not
finished. This closes exactly the one requested milestone; report and stop.
Rollback: `~/Java/BiglyBTC/build/saved-history-20261019/previous-dist`. P04-17 remains qualified at source
`a84e4ecc6f78112c115c182aefc13bc4a1f3ac17`, metadata
`9d6023fca3b322dfbe5d92d1666b7f2120cd7752`, tag
`milestone-engine-activity-20261019`: 53 GCC, 5 Clang and 9 sanitizer suites;
52 engine-activity History rows, 26 View rows and 7 Window/worker rows; 92 Page
and 12 engine rows; and 30 captures across three scales. Its own prior dist rollback at
`~/Java/BiglyBTC/build/engine-activity-20261018/previous-dist` is unchanged.
P04-16 source `5fc34b588713112b126f3cebdfc891020301a281`, metadata
`30569e2631427003a4a92e7edee3572a3b34e740`, tag
`milestone-engine-statistics-20261018` and its 50 GCC, 3 Clang and 9 sanitizer
suites, with 24 captures, remain
historical, with `~/Java/BiglyBTC/build/statistics-engine-20261018/previous-dist`.
P04-15 and all earlier evidence, failed attempts and rollbacks are preserved.
See P04-18 and `migration/evidence/20261019-saved-history/` for actual evidence.
<!-- /p04-18-saved-history-completion -->

<!-- p04-19-history-week-completion -->
P04-19 saved seven-day overview is qualified and published.
Source `136af049bc3ea5307a666ef4b716eaa50d36d1c4`, metadata `94bf1cb1c17ea7979bc56ae21e9837605689f6a1`, annotated tag `milestone-history-week-20261020`.
All 49 changed/new source files match the actual qualified Git map.
GCC 61 unit suites, Clang 12 focused suites and 15 leak-enabled ASan/UBSan suites
pass without skips, retaining every prior 57/8/11 test name. The new date policy's
9 cases and reader's 12 families pass. Three generated profiles characterize
525 independent original-Java member-day/hour queries and 3150 signed scalars.
The earlier daily corpus remains unchanged: 17 files, 425 queries and 2550 scalars.
New seven-day UI: 38 Qt rows; actual Window/worker: 12 rows. Existing Daily UI's
34 and worker's 12 rows, Page's 92, saved Statistics worker's 20, engine worker's
12 and graph worker's 7 rows remain qualified. At each 100%, 150% and 200% scale,
eight offline smoke rows, four actual engine rows and four actual graph rows
pass, producing 14 PNGs per scale, 42 overall. Hosted desktop 39387664142 and SDK
39387664175 pass on the exact source; the independently inspected artifact has 61 units
and 14 captures. The dist's 103 file hashes verify; 82 private runtime files are
unchanged and the complete previous dist is retained. Original Java HEAD/index,
process and live profile are untouched. Offscreen evidence does not establish
manual compositor acceptance.

Daily remains the default. The new view exposes exactly seven independently read
UTC daily files ending at a chosen date, source state and six signed counters.
The actual original Java reader supplied 525 new member-day/hour observations
(3150 scalars); the older 2550 scalar comparisons remain distinct and unchanged.
This is a seven-row overview of independent saved UTC daily files,
ending at a selected date. No weekly total is computed. A whole original multi-day
query can include rollover spill from a prior pathname day; seven independently
clipped member queries are not claimed equal to that aggregate. Original rolling
168-hour graphs, sliding/configured calendar weeks, current-clock end clamp,
cache and live writer are not ported. Missing files, present empty/header-only
files, unknown totals, observed zero and signed adjustments stay distinct.
Native strict shape/version/limits/checked arithmetic and presence/count metadata
receive zero Java equality credit. One failed or cancelled member rejects the
entire batch without partial publication. Seven individually checked reads are
not a transactional multi-file snapshot. No history/config/payload writes,
directory scans, cache.dat, extra lease, new worker, engine or network owner are
introduced. Full-client/plugin, seamless-switching, performance and release gates
remain unfinished.
Broad original first_ported/integrated source coverage remains null. Only
statistics.saved_history_week is implemented; no whole migration completion is
claimed. Raw evidence is protected by scoped attributes; complete staged-byte
verification is required before the first private ref update. This closes exactly
one requested milestone: report and stop.
Rollback: `~/Java/BiglyBTC/build/history-week-20261020/previous-dist`. P04-18 remains qualified at source
`9db774659334daa2dbabd81762c819c108eb43b2`, metadata `fe09d1c9430a5a3c684d34be97a6661d08704c72` and tag
`milestone-saved-history-20261019`: 57 GCC, 8 Clang and 11 sanitizer suites, Daily UI 34
and worker 12 rows, 2550 original-Java scalars and 36 three-scale captures.
Its complete preview is the new rollback above. Its own previous dist at
`~/Java/BiglyBTC/build/saved-history-20261019/previous-dist` and all P04-18 evidence,
including the preserved failed private checkpoint and scoped raw-byte repair,
remain unchanged. P04-17/P04-16 and earlier evidence, source hashes, tags and
rollback directories also remain historical and unchanged.
See P04-19 and `migration/evidence/20261020-history-week/` for actual evidence.
<!-- /p04-19-history-week-completion -->

<!-- p04-20-history-charts-completion -->
P04-20 saved History charts are qualified and published.
Source `889dfe8cc9a22830149d79c7108ce946a7df2766`, metadata `775f84222031c1f43ee96e76c53fff0a5ef1e225`, annotated tag `milestone-history-charts-20261020`.
All 14 changed/new source files and 63 unchanged behavior
inputs match the qualified byte maps. GCC 64 unit suites, Clang 15 focused suites
and 18 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
61/12/15 names. Actual chart data/widget/view totals are 19/13/
22 Qt rows. Existing Daily 34/12, seven-day 38/12, Page 92, saved Statistics
worker 20, engine worker 12 and graph worker 7 rows remain qualified. The prior
2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or equality credit is claimed. At 100%, 150% and 200% scale, eight
offline smoke rows and four actual engine plus four graph rows pass, with 14 PNGs
per scale and 42 overall. Hosted desktop 39392956687 and SDK 39392956684 pass on this source;
the inspected artifact has 64 units and 14 captures. The dist's 103 files verify,
82 private runtime files are unchanged and the entire prior dist is retained.
Original Java HEAD/index, running client and live profile remain untouched.
Offscreen captures do not establish manual compositor acceptance.

This milestone only renders existing saved payload download/upload
facts: 24 UTC hourly buckets in Daily, or seven independent UTC daily-file totals
in 7 days. Unknown values are gaps, recorded zero is real and signed corrections
remain signed. Only adjacent known buckets connect; isolated observations remain
visible. Values are bytes, not rates. No weekly aggregate, interpolation, new
accounting, clock/timer, file read, worker, lease, network or persistence is added.
The original rolling/pixel-merged History queries, width scaling, local-zone axes,
overall period totals, cache/writer and global history service are not replicated.
Seven independently checked reads remain nontransactional. Existing strict native
errors, presence/counts and file safety retain their separate qualification.
Full-client/plugin, seamless switching, performance and broad release gates remain
unfinished.
Only statistics.saved_history_charts is implemented. Broad original first_ported/
integrated coverage remains null. No new Java execution or equality credit is
assigned. Complete rollback: `~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. P04-19 remains qualified at source
`136af049bc3ea5307a666ef4b716eaa50d36d1c4`, metadata `94bf1cb1c17ea7979bc56ae21e9837605689f6a1` and tag
`milestone-history-week-20261020`: 61 GCC, 12 Clang, 15 sanitizer suites and 42 captures.
Its complete dist is retained at `~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. Its own earlier rollback
at `~/Java/BiglyBTC/build/history-week-20261020/previous-dist` and P04-18/P04-17/
P04-16 evidence, commits, tags and rollback directories remain unchanged.
The one requested milestone is complete; report and stop. Actual private staged
bytes must verify before the initial ref update. See P04-20 and
`migration/evidence/20261020-history-charts/` for the actual evidence.
<!-- /p04-20-history-charts-completion -->

<!-- p04-21-history-hourly-completion -->
P04-21 seven-day hourly History resolution is qualified and published.
Source `c35ca6e2470aa8f154ec7676e61734c012725283`, metadata `42b45749abd481014486a33844520c5d670b534b`, annotated tag `milestone-history-hourly-20261020`.
All 10 changed/new source files and 202 unchanged inputs
match the qualified byte maps. GCC 65 unit suites, Clang 16 focused suites and
19 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
64/15/18 names. The new hourly-resolution suite passes 22 actual Qt rows;
prior chart data/widget/view totals remain 19/13/22. Existing Daily 34/12,
seven-day 38/12, Page 92, saved Statistics worker 20, engine worker 12 and graph
worker 7 rows remain qualified. The 2550 and 3150 original-Java scalar comparisons
remain unchanged; no new Java execution or equality credit is claimed. At each
100%, 150% and 200% scale, eight offline smoke rows and four actual engine plus
four graph rows pass, with 16 PNGs per scale and 48 overall. Hosted desktop
39397413125 and SDK 39397413183 pass on this source; the inspected artifact contains
65 units and 16 captures. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor acceptance.

Graph resolution changes only the saved seven-day chart.
Daily totals remains default; Hourly values displays exactly 168 fixed UTC
buckets, oldest selected date at 00:00 through ending date at 23:00. Values copy
optional payload download/upload bytes directly from accepted saved hours; no
daily-total division, rate, sum or weekly aggregate is inferred. Unknown days or
hours are gaps, recorded zero is real and signed corrections remain signed.
Adjacent known hours connect across midnight, with isolated points visible.
The seven-row daily table and date/session/request admission remain unchanged.
Resolution is local to the View lifetime, not configuration persistence. A control
change invalidates the previous render immediately; its replacement is queued until
the control's signal delivery returns. A destroyed View receives no queued render.
Switching adds no read, clock, timer, worker, lease, network, writer/cache or accounting.
The original current-clock, hour-rounded, pixel-merged week graph and global
history service are not replicated. Existing strict errors and presence/file
safety remain separately qualified. Seven reads remain nontransactional.
Full-client/plugin, seamless switching, performance and broad release gates
remain unfinished.
Only statistics.saved_history_hourly is implemented. Broad original first_ported/
integrated coverage remains null. No new Java execution or equality credit is
assigned. Complete rollback: `~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist`. P04-20 remains qualified at source
`889dfe8cc9a22830149d79c7108ce946a7df2766`, metadata `775f84222031c1f43ee96e76c53fff0a5ef1e225` and tag
`milestone-history-charts-20261020`: 64 GCC, 15 Clang, 18 sanitizer suites, chart rows 19/13/22
and 42 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist`. Its own
earlier rollback at `~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`
and all P04-19/P04-18/P04-17/P04-16 evidence, commits, tags, diagnostic failures
and rollback directories remain unchanged.
The one requested milestone is complete; report and stop. Actual private staged
bytes must verify before the initial ref update. See P04-21 and
`migration/evidence/20261020-history-hourly/` for the actual evidence.
<!-- /p04-21-history-hourly-completion -->

<!-- p04-22-statistics-navigation-completion -->
P04-22 Statistics navigation on reopening is qualified and published.
Source `f7d979801773934d481155019b18319b64beff96`, metadata `11dd37171a68c9de8bdae662cc2daf630f363120`, annotated tag `milestone-statistics-navigation-20261020`.
All 16 changed/new source files and 221 unchanged inputs
match the qualified byte maps. GCC 67 unit suites, Clang 18 focused suites and
21 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
65/16/19 names. New navigation Page/Window suites pass 13/11
actual Qt rows. Retained hourly resolution passes 22 rows and chart data/widget/view
remain 19/13/22. Daily 34/12, seven-day 38/12, Page 92, saved Statistics worker 20,
engine worker 12 and graph worker 7 rows remain qualified. The 2550 and 3150
original-Java scalar comparisons remain unchanged; no new Java execution or
navigation equality credit is claimed. At each 100%, 150% and 200% scale, nine
offline smoke rows and four actual engine plus four graph rows pass, with 18 PNGs
per scale and 54 overall. Hosted desktop 39403289482 and SDK 39403289692 pass on this
source; the inspected artifact contains 67 units and 18 captures. The dist's
103 files verify, 82 private runtime files are unchanged and the entire prior
dist is retained. Original Java HEAD/index, running client and live profile remain
untouched. Offscreen captures do not establish manual compositor acceptance.

Closing Statistics remembers only scalar navigation within the same
nonzero profile session: top section, Activity Payload/Engine traffic, Transfers
Current/History, History Daily/7 days, both selected UTC dates and seven-day
Daily totals/Hourly values. Hidden subview choices are included. Reopening creates
a fresh Page and performs the existing fresh saved-total and selected-History
reads; no closed Page, saved result packet or history cache is retained by this
navigation record. Existing engine Activity history retention is unchanged.
Accepted monotonic session advancement clears closed-tab memory before callbacks;
zero, duplicate and older sessions are ignored. A still-open Page keeps its
existing navigation and session data-reset behavior. Closing it later records its
current choices for the new session. Reusing an already-open Page is unchanged.
This is per-Window memory, not application restart or configuration persistence.
Existing request identities, busy admission, latest-pending coalescing, worker and
read-only file boundaries remain unchanged. No new reader, worker, lease, cache,
timer, accounting, configuration/history/payload write or weekly total is added.
Both original Java corpora remain retained evidence of their earlier scalar
observations, not equality for this Qt navigation adaptation. Original SWT/MDI
persistence, current-clock rolling history, global recording, full-client/plugin,
seamless switching, performance and broad release gates remain unfinished.
Only statistics.navigation_state is implemented. Broad original first_ported/
integrated coverage remains null. No new Java execution or equality credit is
assigned. Complete rollback: `~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`. P04-21 remains qualified at source
`c35ca6e2470aa8f154ec7676e61734c012725283`, metadata `42b45749abd481014486a33844520c5d670b534b` and tag
`milestone-history-hourly-20261020`: 65 GCC, 16 Clang, 19 sanitizer suites, hourly resolution
22 rows, chart rows 19/13/22 and 48 captures. Its complete dist is retained at
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`. Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-20 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
The one requested milestone is complete; report and stop. Actual private staged
bytes must verify before the initial ref update. See P04-22 and
`migration/evidence/20261020-statistics-navigation/` for the actual evidence.
<!-- /p04-22-statistics-navigation-completion -->

<!-- p04-23-history-traffic-completion -->
P04-23 saved History traffic selection is qualified and published.
Source `1b08cceee82df9d29f1b35719a6ac24f2c6c2bc1`, metadata `bd27aeba82c48c8cd4bddcd8ca35d9f48fb7f310`, annotated tag `milestone-history-traffic-20261020`.
All 15 changed/new source files and 232 unchanged inputs
match the qualified byte maps. GCC 69 unit suites, Clang 20 focused suites and
23 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
67/18/21 names. New traffic data/View suites pass 10/14 actual
Qt rows. Retained navigation Page/Window rows remain 13/11, hourly resolution 22
and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12, Page 92, saved
Statistics worker 20, engine worker 12 and graph worker 7 rows remain qualified.
The 2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or UI equality credit is claimed. At each 100%, 150% and 200% scale,
ten offline smoke rows and four actual engine plus four graph rows pass, with
20 PNGs per scale and 60 overall. Hosted desktop 39442937594 and SDK 39442937460 pass on
the qualified source; the inspected artifact contains 69 units and 20 captures.
The metadata-only SDK workflow 39444099940 also passes on the
published metadata commit. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor acceptance.

Traffic selects Payload (default), Protocol or DHT independently for
Daily and seven-day saved History charts; the seven-day choice applies to both
daily totals and hourly resolution. Each graph copies the accepted saved signed
download/upload fields directly: Payload indices 3/1, Protocol 2/0 and DHT 5/4.
No combined sum, rate conversion, inferred zero or weekly aggregate is introduced.
Unknown buckets remain gaps, recorded zero stays real and signed corrections and
extremes retain their meaning. Tables still show the same six saved counters;
Daily summary labels and values remain explicitly payload even for another chart
traffic choice. Existing dates, resolutions and accepted result packets are unchanged.
A traffic change invalidates revision immediately and queues a guarded render
without another read, date/session/request change or model reset. Hidden/minimized
views defer rendering. Default calls continue to select Payload. Both traffic
choices join existing per-Window scalar navigation, including hidden choices,
and restore only on same nonzero-session tab reopening through fresh saved reads.
Accepted session advancement clears closed-tab memory; a visible Page keeps its
existing navigation/data-reset behavior. No widgets or saved result packets are
retained by this memory, and no configuration or restart persistence is added.
Core accounting, readers, workers, both prior Java corpora and their strict native
file/error policies remain unchanged. Seven file reads remain nontransactional.
Original Java UI/rendering equality, rolling history/global recording, keyboard
inspection and copy controls, full-client/plugin, seamless switching, performance
and broad release gates are not established by P04-23.
Only statistics.history_traffic is implemented by this milestone. Broad original
first_ported/integrated coverage remains null. No new Java execution or equality
credit is assigned. Complete rollback: `~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`. P04-22 remains qualified at source
`f7d979801773934d481155019b18319b64beff96`, metadata `11dd37171a68c9de8bdae662cc2daf630f363120` and tag
`milestone-statistics-navigation-20261020`: 67 GCC, 18 Clang, 21 sanitizer suites, navigation 13/11
rows and 54 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-21 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-23 is milestone 1 of the current three-milestone request.
Continue with P04-24 keyboard chart inspection, then P04-25 copying selected
History chart values. Each requires its own reviewed scope, local/hosted
qualification, promoted portable preview with complete rollback, source/metadata/tag
publication and private checkpoint. Do not declare the current request complete
after P04-23; stop after all three authorized milestones are completed.
Actual private staged bytes must verify before the initial ref update. See
P04-23 and `migration/evidence/20261020-history-traffic/` for actual evidence.
<!-- /p04-23-history-traffic-completion -->

<!-- p04-24-history-keyboard-completion -->
P04-24 saved History keyboard inspection is qualified and published.
Source `07219078bf3c5b3ed1cca7e701dcbdfeda8e687d`, metadata `05d460afde14e9a3510fe2f6d823422fe4edd642`, annotated tag `milestone-history-keyboard-20261020`.
All 7 changed/new source files and 241 unchanged inputs
match the qualified byte maps. GCC 70 unit suites, Clang 21 focused suites and
24 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
69/20/23 names. The new keyboard suite passes 17 actual Qt rows. Retained
traffic data/View rows remain 10/14, navigation Page/Window 13/11, hourly resolution
22 and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12, Page 92, saved
Statistics worker 20, engine worker 12 and graph worker 7 rows remain qualified.
The 2550 and 3150 original-Java scalar comparisons remain unchanged; no new Java
execution or UI equality credit is claimed. At each 100%, 150% and 200% scale,
eleven offline smoke rows and four actual engine plus four graph rows pass, with
21 PNGs per scale and 63 overall. Hosted desktop 39448068109 and SDK 39448068100 pass on
the qualified source; the inspected artifact contains 70 units and 21 captures.
The metadata-only SDK workflow 39449473199 also passes on the
published metadata commit. The dist's 103 files verify, 82 private runtime files
are unchanged and the entire prior dist is retained. Original Java HEAD/index,
running client and live profile remain untouched. Offscreen captures do not
establish manual compositor or real screen-reader acceptance.

A focused, visible saved History chart accepts unmodified Left/Right/Home/End
for exact UTC bucket inspection. Daily, seven-day totals and seven-day hourly
charts expose 24, 7 and 168 selectable buckets, including unknown gaps. First
Left/Right/Home selects the first bucket; End selects the last. Subsequent keys
clamp without wrapping. An elided caption shows the selection; the full tooltip and accessible
description identify the date/hour, chosen traffic and exact signed
download/upload values.
Unknown remains Unknown, recorded zero stays real and signed extremes are not
rounded by narration. Focus loss retains selection/narration but hides focus-only
guides and known point outlines. Unknown has no invented Y point. Hover retains
its existing tooltip behavior without moving the keyboard selection. Accepted
frame replacement clears selection. Reentrant frame/key/property/accessibility
callbacks are fenced by object lifetime, frame identity and interaction revision.
These are native offscreen behavior checks, not real assistive-technology acceptance.
Keyboard inspection adds no read, request, timer, worker, configuration write or
retained source packet. It is per-frame interaction state, not added to the
same-session scalar navigation record. Existing per-Window dates, tabs, resolution
and independent Daily/seven-day traffic choices still restore through fresh reads.
Accepted session advancement clears closed-tab memory; a visible Page keeps its
existing navigation/data-reset behavior. No widgets or saved result packets are
retained, and no restart persistence is added. Traffic still copies exact saved
fields: Payload indices 3/1, Protocol 2/0 and DHT 5/4, without combined sums, rates
or additional reads. Tables and explicitly payload summaries are unchanged.
Core accounting, readers, workers, both original Java corpora and strict native
file/error policies remain unchanged. Seven daily file reads are nontransactional.
No weekly aggregate, new Java execution or original Java UI equality is claimed.
Copy controls belong to P04-25. Rolling/global history, full-client/plugin parity,
seamless profile switching, performance and broad release gates remain open.
Only statistics.history_keyboard is implemented by this milestone. Broad original
first_ported/integrated coverage remains null. No new Java execution or equality
credit is assigned. Complete rollback: `~/Java/BiglyBTC/build/history-keyboard-20261020/previous-dist`. P04-23 remains qualified at source
`1b08cceee82df9d29f1b35719a6ac24f2c6c2bc1`, metadata `bd27aeba82c48c8cd4bddcd8ca35d9f48fb7f310` and tag
`milestone-history-traffic-20261020`: 69 GCC, 20 Clang, 23 sanitizer suites, traffic data/View
rows 10/14 and 60 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-keyboard-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`,
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-22 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-24 is milestone 2 of the current three-milestone request.
Continue only with P04-25 copying selected History chart values. It requires its
own reviewed scope, actual local/hosted qualification, promoted portable preview
with complete rollback, source/metadata/tag publication and private checkpoint.
The current request remains active; do not declare it complete after P04-24.
Stop after P04-25 completes the third and final authorized milestone.
Actual private staged bytes must verify before the initial ref update. See
P04-24 and `migration/evidence/20261020-history-keyboard/` for actual evidence.
<!-- /p04-24-history-keyboard-completion -->

<!-- p04-25-history-copy-completion -->
P04-25 explicit saved History Copy is qualified and published.
Source `ab0ff75d81300d8d3f9e930a9bf55491c1801400`, metadata `3635fe01fa0212e075af5c956740e499303980b9`, annotated tag `milestone-history-copy-20261020`.
All 6 changed/new source files and 243 unchanged inputs
match the qualified byte maps. GCC 71 unit suites, Clang 22 focused suites and
25 leak-enabled ASan/UBSan suites pass without skips, retaining all prior
70/21/24 names. The new Copy suite passes 19 actual Qt rows. Retained keyboard
rows remain 17, traffic data/View 10/14, navigation Page/Window 13/11, hourly
resolution 22 and chart data/widget/view 19/13/22. Daily 34/12, seven-day 38/12,
Page 92, saved Statistics worker 20, engine worker 12 and graph worker 7 rows remain
qualified. The 2550 and 3150 original-Java scalar comparisons remain unchanged;
no new Java execution or UI equality credit is claimed. At each 100%, 150% and
200% scale, twelve offline smoke rows and four actual engine plus four graph rows
pass, with 22 PNGs per scale and 66 overall. Hosted desktop 39450607631 and SDK 39450607673
pass on the qualified source; the inspected artifact contains 71 units and 22
captures. The metadata-only SDK workflow 39452631555 also passes
on the published metadata commit. The dist's 103 files verify, 82 private runtime
files are unchanged and the entire prior dist is retained. Original Java
HEAD/index, running client and live profile remain untouched. Offscreen clipboard,
keyboard and captures do not establish real compositor/system clipboard or
screen-reader acceptance.

A saved History plot exposes Copy selected values through its context menu
and the normal Copy shortcut (Ctrl+C on the tested Linux platform), scoped to the
focused widget. It starts disabled and enables only for a valid selected current
bucket, including Unknown. Focus loss retains the selection and context action.
Accepted frame replacement clears selection and disables Copy; a fresh reopened
Page begins without a per-frame selection. No user action means no clipboard change.
Explicit Copy publishes exactly the immutable selected bucket's existing tooltip:
UTC day/hour, Payload/Protocol/DHT family and signed decimal downloaded/uploaded
bytes or Unknown. It does not copy the currently hovered bucket by accident,
convert units, infer zero, build HTML, combine counters or calculate a weekly sum.
Only QClipboard::Clipboard is written, never Selection or FindBuffer. QAction
changed-signal and clipboard-notification reentry/deletion paths use object,
frame, interaction revision and selected-index guards; the setter is the final
receiver operation. Arbitrary deletion from Qt-internal ActionChanged event filters
is outside the reviewed signal-callback qualification seam. Generated tests use
the isolated offscreen clipboard with RAII restoration, not a real user clipboard.
Copy adds no saved read, timer, worker, file/config write or retained source packet.
Existing Left/Right/Home/End inspection clamps across 24/7/168 buckets, preserves
signed/Unknown narration and clears on accepted frame replacement. Dates, tabs,
resolution and independent Daily/seven-day traffic choices still restore within
the same nonzero profile session through fresh reads. Closed-tab memory clears
on accepted session advancement; visible-Page session behavior remains unchanged.
Core accounting, readers, workers, both original Java corpora and strict native
file/error policies are unchanged. Seven file reads remain nontransactional.
No new Java execution/UI equality, global/rolling history, restart persistence,
seamless profile switching, full-client/plugin parity, performance or broad release
qualification is established. This completes only the three requested UI milestones.
Only statistics.history_copy is implemented by this milestone. Broad original
first_ported/integrated coverage remains null. No new Java execution or equality
credit is assigned. Complete rollback: `~/Java/BiglyBTC/build/history-copy-20261020/previous-dist`. P04-24 remains qualified at source
`07219078bf3c5b3ed1cca7e701dcbdfeda8e687d`, metadata `05d460afde14e9a3510fe2f6d823422fe4edd642` and tag
`milestone-history-keyboard-20261020`: 70 GCC, 21 Clang, 24 sanitizer suites, keyboard 17 rows
and 63 captures. Its complete dist is retained at `~/Java/BiglyBTC/build/history-copy-20261020/previous-dist`.
Earlier complete rollbacks remain at
`~/Java/BiglyBTC/build/history-keyboard-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-traffic-20261020/previous-dist`,
`~/Java/BiglyBTC/build/statistics-navigation-20261020/previous-dist`,
`~/Java/BiglyBTC/build/history-hourly-20261020/previous-dist` and
`~/Java/BiglyBTC/build/history-charts-20261020/previous-dist`. All P04-23 and earlier
evidence, commits, tags, diagnostic failures and rollback directories remain unchanged.
P04-23 traffic selection, P04-24 keyboard inspection and P04-25 explicit Copy
are the three completed milestones of this request. Stop this request after its
actual final private checkpoint; do not begin a fourth milestone without new user
direction. The broader migration and release epic remain unfinished. Open work is
recorded separately; no current user request or release-parity completion is inferred.
Actual private staged bytes must verify before the initial ref update. See
P04-25 and `migration/evidence/20261020-history-copy/` for actual evidence.
<!-- /p04-25-history-copy-completion -->


## Completed request: two 30-day Statistics milestones

<!-- thirty-day-plan-progress -->
Both authorized milestones, P04-26/P04-27, are qualified and published.
Root records the actual final raw-byte private checkpoint after these updates,
then stops this request. No third milestone is authorized by this request.
No future or self-referential private commit is invented. The broader migration
and release epic remain open; further work awaits new user direction.
<!-- /thirty-day-plan-progress -->

Original request wording (historical):

The user renewed authorization for two milestones after P04-25: read-only fixed
30-day saved history (P04-26), then its Statistics UI/worker integration (P04-27).
Follow the frozen contract in `build/history-thirty-next-two-20261020/contract.md`;
qualify and publish each separately and stop after both. Preserve all previous
evidence/rollbacks and original running Java client/profile. No full-client or
rolling/global-history parity is implied.

<!-- p04-26-saved-history-month-completion -->
## P04-26: Read-only fixed 30-day saved-history backend

Source `d36dd2d1c92914c796e20115939e3c66e7627ba1`, metadata `e53678361d8903f1e34b0a0c39623c78bd7dff7c`,
annotated tag `milestone-history-month-backend-20261020`. All 9 source files and
246 unchanged inputs match their qualified maps.
GCC/Clang/leak-enabled ASan+UBSan suites pass 73/24/27 with zero skips, retaining
every predecessor 71/22/25 unit name. New suites: saved-history-month-dates: 11 actual case families, saved-history-month-reader: 14 actual case families.
Retained daily/seven-day original-Java scalar comparisons remain 2550/3150;
keyboard 17, Copy 19 and all earlier named regression suites remain required.
No new Java execution or UI-equality credit is assigned. At 100%,150%,200%,
12 offline smoke rows and four engine plus four graph rows pass per scale:
22 PNGs per scale, 66 local captures overall.
Hosted desktop 39457365591 and SDK
39457365491 pass on the exact source, with an API/digest/
ZIP/tree-byte-bound artifact containing 73 unique units and
22 captures. Metadata SDK 39458345089 passes on the
published metadata commit. Dist verifies all 103 files, 82 runtime files remain
byte-identical, exact installed RPATH replay passes, and the complete prior dist
is retained. Original Java HEAD/index, running client and live profile remain untouched.

This milestone adds only the pure date-range and held-profile reader backend.
The runnable desktop still exposes the previously qualified Daily and 7 days
views; the 30-day tab and worker/UI integration are P04-27, not credited here.
The retained desktop, keyboard, explicit Copy and engine regressions were rerun;
their existing features are not new backend functionality.

The native range is exactly 30 independent UTC daily records, not a variable
calendar month or an aggregate. Missing files, present files without observations,
known zero and signed adjustments remain distinct; all six saved counter
components are retained. Dates are oldest first and end inclusively, wholly
within 1970..9999. One held lease is borrowed and the unchanged daily reader is
called sequentially. Malformed, unsafe, IO and cancellation failures produce no
partial successful range. The reads are not a transactional multi-file snapshot.
No payload stat/read, directory scan, clock clamp, cache, writer, global recorder
or additional lease is added. Original TS_MONTH selects 720 hourly periods before
pixel merging; this native 30-daily-record view has different resolution and no
original monthly graph/query/UI equality claim. The original 425/2550 and 525/3150
receipts remain in their historical namespaces, unchanged. Broad first_ported and
integrated_through coverage remains null. Full-client/plugin, switching, rolling
global history, performance and broad release qualification remain unfinished.

The immediate predecessor is source `ab0ff75d81300d8d3f9e930a9bf55491c1801400`,
metadata `3635fe01fa0212e075af5c956740e499303980b9`, tag `milestone-history-copy-20261020`; its whole dist is
retained at `~/Java/BiglyBTC/build/history-month-backend-20261020/previous-dist`. Earlier receipts, failures, source capsules, commits,
tags, checkpoints and rollback directories stay unchanged. The predecessor's
actual private checkpoint `0666c1846d86367c2ec7eb0c119382e85d79148b` is this phase's parent;
this phase's final private identity is recorded separately after staged-byte proof.

Milestone 1 of the two requested milestones is qualified and published.
Root must record its actual raw-byte-verified private checkpoint after these
documentation updates; P04-27 may start only from that completed milestone.
Continue only with the authorized30-day UI milestone, then stop after its final
checkpoint. No broader migration or release completion is inferred.

Evidence: [local verification](migration/evidence/20261020-history-month-backend/local-verification.json),
[hosted verification](migration/evidence/20261020-history-month-backend/hosted-verification.json) and
[publication](migration/evidence/20261020-history-month-backend/publication.json).
Public [milestone](https://github.com/d00rsfan/BiglyBTC/blob/e53678361d8903f1e34b0a0c39623c78bd7dff7c/milestones/history-month-backend-20261020.json).
Recipes remain in `build/history-month-backend-20261020` and the shared documentation helper in
`build/history-thirty-next-two-20261020/finalize_docs.py`.
<!-- /p04-26-saved-history-month-completion -->

<!-- p04-27-history-month-ui-completion -->
## P04-27: Statistics: 30 independent saved UTC days

Source `ddc6bb022095866eceeddf2d8598201e07652c28`, metadata `0d3fc031d6bf97c39a4a7f6ced0c6151bc4af873`,
annotated tag `milestone-history-month-ui-20261020`. All 24 source files and
238 unchanged inputs match their qualified maps.
GCC/Clang/leak-enabled ASan+UBSan suites pass 76/27/30 with zero skips, retaining
every predecessor 73/24/27 unit name. New suites: saved-history-month-plot-data: 13 actual Qt rows, saved-history-month-page: 25 actual Qt rows, saved-history-month-window-worker: 12 actual Qt rows.
Retained daily/seven-day original-Java scalar comparisons remain 2550/3150;
keyboard 17, Copy 19 and all earlier named regression suites remain required.
No new Java execution or UI-equality credit is assigned. At 100%,150%,200%,
13 offline smoke rows and four engine plus four graph rows pass per scale:
23 PNGs per scale, 69 local captures overall.
Hosted desktop 39461888661 and SDK
39461888560 pass on the exact source, with an API/digest/
ZIP/tree-byte-bound artifact containing 76 unique units and
23 captures. Metadata SDK 39463131398 passes on the
published metadata commit. Dist verifies all 103 files, 82 runtime files remain
byte-identical, exact installed RPATH replay passes, and the complete prior dist
is retained. Original Java HEAD/index, running client and live profile remain untouched.

Tools > Statistics > Transfers > History > 30 days displays 30 fixed rows with
saved state plus the six raw counters. Its chart has 30 daily buckets and chooses
Payload, Protocol or DHT without another read or a combined sum. Previous/Next
move 30 days; ending dates span 1970-01-30..9999-12-31. Keyboard inspection and
explicit Copy reuse the qualified immutable-frame behavior, including Unknown
and signed values. Hidden/minimized rendering is deferred. Generation/session/
kind/date gates, one shared active optional-statistics slot, coalescing, cancellation,
close/reopen and deletion handling are qualified only within this owned preview.
Same-session navigation retains scalar month date/traffic choices; reopening
uses fresh reads and keeps no widget, packet or cursor in navigation memory.
No restart/configuration persistence or new live history service is implied.

The native range is exactly 30 independent UTC daily records, not a variable
calendar month or an aggregate. Missing files, present files without observations,
known zero and signed adjustments remain distinct; all six saved counter
components are retained. Dates are oldest first and end inclusively, wholly
within 1970..9999. One held lease is borrowed and the unchanged daily reader is
called sequentially. Malformed, unsafe, IO and cancellation failures produce no
partial successful range. The reads are not a transactional multi-file snapshot.
No payload stat/read, directory scan, clock clamp, cache, writer, global recorder
or additional lease is added. Original TS_MONTH selects 720 hourly periods before
pixel merging; this native 30-daily-record view has different resolution and no
original monthly graph/query/UI equality claim. The original 425/2550 and 525/3150
receipts remain in their historical namespaces, unchanged. Broad first_ported and
integrated_through coverage remains null. Full-client/plugin, switching, rolling
global history, performance and broad release qualification remain unfinished.

The immediate predecessor is source `d36dd2d1c92914c796e20115939e3c66e7627ba1`,
metadata `e53678361d8903f1e34b0a0c39623c78bd7dff7c`, tag `milestone-history-month-backend-20261020`; its whole dist is
retained at `~/Java/BiglyBTC/build/history-month-ui-20261020/previous-dist`. Earlier receipts, failures, source capsules, commits,
tags, checkpoints and rollback directories stay unchanged. The predecessor's
actual private checkpoint `b1b72df3534bb16c9b7abdc66b897805d3435e3d` is this phase's parent;
this phase's final private identity is recorded separately after staged-byte proof.

Both authorized milestones, P04-26/P04-27, are qualified and published.
Root records the actual final raw-byte private checkpoint after these updates,
then stops this request. No third milestone is authorized by this request.
No future or self-referential private commit is invented. The broader migration
and release epic remain open; further work awaits new user direction.

Evidence: [local verification](migration/evidence/20261020-history-month-ui/local-verification.json),
[hosted verification](migration/evidence/20261020-history-month-ui/hosted-verification.json) and
[publication](migration/evidence/20261020-history-month-ui/publication.json).
Public [milestone](https://github.com/d00rsfan/BiglyBTC/blob/0d3fc031d6bf97c39a4a7f6ced0c6151bc4af873/milestones/history-month-ui-20261020.json).
Recipes remain in `build/history-month-ui-20261020` and the shared documentation helper in
`build/history-thirty-next-two-20261020/finalize_docs.py`.
<!-- /p04-27-history-month-ui-completion -->

## Historical broader authorization: finish Statistics (2026-10-21)

Superseded by the latest explicit instruction: complete only P04-29 and STOP. P04-30 and subsequent implementation are not started.

The new request supersedes the preceding two-milestone stop. Inventory the
entire original StatsView, implement the remaining Activity/Transfers and
service-backed statistics through reviewed stages, and keep exact source/
qualification/checkpoint evidence. Saved daily browsers alone do not establish
rolling queries, application accounting, original cache/DHT/Vivaldi or full
Statistics parity. Keep the original live client/profile outside all tests.

<!-- persistent-peer-accounting-current -->
## Current milestone: P04-29 persistent native peer owner (2026-10-21)

The requested bounded backend milestone is qualified and published. STOP after
its private evidence checkpoint; do not start P04-30 or another milestone.
Full Statistics, Activity/Transfers presentation and compatible writers remain
unfinished. There are no pending user questions.

Native source `b0c7f259bca2043df4fb1297e3440f2e5eb1a270`, metadata `01973075251a3078f45973318fb841422ff25f7c`,
tag `milestone-persistent-peer-accounting-20261021` (object `dab160402c8dc0d9c04ad3eba46ea01e78ea66f5`).
All 53 changed/new source inputs and 360 retained guards match immutable Git bytes.
DEFAULT 85, instrumented GCC 15, Clang 49 and leak-enabled ASan/UBSan 52 CTests
pass without skips; the retained source-bound parent cohort passes 13 suites.
Actual Qt rows for engine/app/API faults/close/causal/policy are 12/9/20/6/5/15.
All 22 actual instrumented admission cases and five generated-profile native/Java
handoffs pass. Failed v1 combined-gate evidence and every correction remain intact.

Hosted desktop 39618288237 and SDK
39618288148 pass on the exact source. Hosted DEFAULT
85 units and 26 captures are API/digest/ZIP/extracted-byte bound. The promoted
portable tree verifies 103 files, 82 unchanged private runtime files, both GUI and
headless exact RUNPATH replays, 13 existing offscreen smoke rows and 19 images.
Prior P04-28's 78 three-scale captures are inherited, not rerun by P04-29.
Dist manifest SHA256 `fc3c012766af59d689d501ed139ee87327fac9091a829a77bd28200b214777b9`. Complete rollback:
`~/Java/BiglyBTC/build/persistent-peer-accounting-20261021/publication/previous-dist`.

This is the source/runtime-bound persistent peer backend prerequisite: one
worker-owned engine serves sequential operations and idle observations, with
explicit retirement and honest uncertainty. Counter checkpoints depend on the
reviewed exact provider/ownership/fresh-response conditions. A retirement fence
is not hidden metadata-flush or crash durability proof; certification effort
deadlines do not bound the underlying join. Portable builds stay unbound and use
the existing per-operation fallback. No new application-session widgets,
lifetime/mark/minute writer, DHT/IP/tracker accounting or whole Java global
statistics port is claimed. P04-30 and Overall oracle materials remain preserved
preparation only, with no adoption credit. Original Java HEAD/index, running
client and live profile remain untouched.

Evidence: [local](migration/evidence/20261021-persistent-peer-accounting/local-verification.json),
[final](migration/evidence/20261021-persistent-peer-accounting/final-verification.json),
[hosted](migration/evidence/20261021-persistent-peer-accounting/hosted-verification.json),
[publication](migration/evidence/20261021-persistent-peer-accounting/publication.json).

<!-- /persistent-peer-accounting-current -->

## 2026-10-28: owned Mainline PORT and volatile Azureus DB

Completed two bounded dependencies at native18c3e0048d381748c6a4154b3d6e9100b6ec2dda: actual peer PORT bootstrap/outgoing independent listener, and explicitly enabled owned-loopback volatile token STORE→DB→FIND_VALUE. Actual qualification: GCC23/Clang5/leak-enabled ASan+UBSan5 selected suites, plus relocated private Qt/provider five binaries and118manifest leaves. Only corrected P06 test expectations were rerun; unchanged successful binary/source proofs were retained explicitly. Initial configure/compile/test failures remain evidence. See migration/evidence/20261028-mainline-port/REVIEW.md and migration/evidence/20261028-azureus-volatile-database/REVIEW.md. Full phases remain open; continue queued saved-library BEP33 scrape and normal Azureus read flags/storage-key stats next under the whole-plan authorisation. BEP33 primitive GCC/Clang/SAN unit and2076 original Java comparisons already passed independently and await next source integration.

## 2026-10-28 bounded queued scrape and volatile storage-key stats

Native implementation `6ce3b29007a3e7fa32572224d8ab8764f0bad71b`, actual qualified source `ae79d7f8991e7444407462697526e0089ed42e65`: gcc:19 accepted (1 from this receipt, 18 reused), clang:4 accepted (1 from this receipt, 3 reused), san:4 accepted (1 from this receipt, 3 reused). Every accepted suite points to its actual selected passed JUnit case and execution source. GCC/Clang initial r3 whole CTest failures and the later SAN DB failure remain failures. Each origin is verified through the two exact test-only followups: Mainline was actually rerun on GCC/Clang at915, and the complete DB suite was actually rerun on all three lanes at the final source. Other credited suites keep their actual earlier source and byte-identical binary proofs. The failed SAN invocation contributes only its three individually passed suites. No claim is made that all19/4/4 suites ran again at the final source. The DB same-IP port-modulo identity collision is a source-proven fixture defect consistent with the failure; the failed invocation did not record the colliding IDs. No aggregate JUnit is generated. Five private copied binaries ran; private Mainline executed only the two named real-wire/cancellation methods (plus Qt initialization/cleanup), not the full suite. Exact per-run commands, sources, pre/post images, XML, initial failures and private-loader proof are retained. Provider `9fbb550bbc1f877ce34ce21849deab97971f5d3eacfbc531a7b777d28a3b2eea` /binding `1267bf58a235dc6c32d9ffadab8bbd994ec81f2aa958f0cf621f8a71e4f6c84c` is reused, not rebuilt. Prior original BEP33 2076 and storage-stat269 projections were already compared through standalone GCC/Clang/ASan+UBSan; this cohort grants no repeat-Java credit. Helper-only initial javac failures and amendments are retained; unchanged/extracted original methods and inert harness boundaries remain explicit in source pins.

Queued scrape covers held saved-library eligibility, exact selected/raw identity, source/admission revocation, bounded known-live-node KRPC response correlation, BFse versus BFsd dialects, typed UI estimate and cancellation. It supplies no payload/piece/resume/rank/tracker authority. Iterative closest-node traversal, configured regular-tracker/active-enableannounce authority, public/IPv6/proxy deployment and full original plugin lifecycle remain open; terminal cookie reclamation has bounded owned acceptance evidence, not unrestricted public runtime parity.

Azureus normal flag reads preserve original seed/downloading/unknown flag behavior. Explicit suspended-diversification volatile storage-key stats use actual Mapping events, original unique-IP bloom/RPM and 14-byte wire values with real bound local contact. Default absent-storage stats return an empty result. Full frequency/size diversification, persistence/expiry/random storage-manager behavior, public activation and whole P06 remain open. Existing DbValue/Mapping and all broader registry baselines are unchanged.

Both new components remain in_progress. No whole phase, shipping, public-network, full Java-runtime or performance parity is claimed. Whole-plan continuation remains authorised; no chapter STOP is inserted.

Evidence: migration/evidence/20261028-mainline-scrape-storage-stats/verification.json.

## 2026-10-28 bounded active Mainline scrape and passive readers

Native implementation `91a1bb98ca1464eec871cb066107c2cf845d8e56`, final test integration `36bb5613aea66cf6e10ec20f51c4f5dfba03a612`: actual per-suite origins GCC9/Clang4/SAN7; only GCC/SAN managed-queue reran after an owned HTTP fixture correction. Three private full QtTest binaries and actual118-leaf private development promotion are retained. Configured/never-admitted/public/dual-family activation, the BF-only original caller gate and full P05/P06 remain open. No upstream baselines advance. Evidence: `migration/evidence/20261028-mainline-active-scrape/verification.json` and `migration/evidence/20261028-mainline-active-scrape/REVIEW.md`.


## 2026-10-29 original identities and owned tracker transports

Native source `210148e403a9e3b0b539d0c195a44e5bb661ba6c` passed fresh
GCC/Clang/ASan+UBSan 19/16/19 selected suites and 103/80/103 Qt method rows.
Original root creation/retained Host lineage, IPv4/IPv6 wire/routing separation,
default-interface planning and saved-only UDP/HTTP(S) scrapes now have executed
integration evidence. A real cold IPv6 socket-selection defect was corrected
in the native provider; the fresh provider passed 28 controls. Current-source
extracted offline Tor passed GCC and sanitizers. Three private runtime checks
passed with the packaged Qt TLS backend. The 119-leaf dist replaced the prior
preview, which was deleted; no rollback preview is retained.

The normal application was launched with a fresh, genuine empty `.bitlybtc`
profile and separate loopback TCP/UDP port 60481. It contains no sample torrents.
The running Java client and real profile/payload remain untouched. See
[milestone evidence](migration/evidence/20261029-mainline-owned-trackers/REVIEW.md).

Continue the whole plan: the reviewed seven-path saved HTTP Basic credentials
and saved JKS trust batch is next, with focused HTTP/queue/network tests rather
than another unrelated Tor/provider/full-cohort replay. P05–P13 remain open;
this checkpoint is not a chapter stop or release-completion claim.


## 2026-10-29 saved tracker credentials and trust

Native source `c7a6ab6700666b86cbf2a0a265cb95addb57a660` passed focused
GCC/Clang/sanitizer 3/2/3 suites; Clang HTTP is compile-only. Each executed
HTTP suite passed 28 Qt rows covering 25 methods, including saved Basic
credential isolation and JKS trust/revocation. Two focused HTTPS selectors
passed with relocated private Qt/TLS/provider images. The 119-leaf dist was
replaced and its predecessor deleted. See
[evidence](migration/evidence/20261029-http-auth-trust/REVIEW.md).

No unrelated Tor/Mainline suites were replayed or credited as current. Next
batch combines reviewed Tor control-loss cleanup, narrow proven Qt Wayland
console handling and original tracker tier selection/failover. An actual
JDK-produced truststore fixture will join that build rather than trigger an
extra compile. Whole-plan work continues; remaining compatibility limits stay
open in the evidence and QUESTIONS.md.

## 2026-10-29 saved mixed tracker tiers and initial I2CP messages

Implementation `ef494c64add015ea84df93ebcfbff1c7a38fa10b` passed fresh GCC5/Clang4/ASan+UBSan5 affected suites; Clang full tracker-owner remains compile-only. Actual private HTTP→UDP, UDP→HTTP and HTTPS/JKS rows passed. Initial I2CP framing and typed messages matched twelve actual original Java writer/parser cases per official and modified reference, with authentication explicitly unsupported. Evidence: migration/evidence/20261029-mixed-i2cp/verification.json. One current dist replaced the prior distribution; the old copy and copied test support were deleted. Active multi-URL selection, connected I2CP client/router and P05–P13 remain open. Next work combines current-admission per-URL observations/current single-helper UDP selection with a bounded initial I2CP TCP channel; no new provider build is assumed.

## 2026-10-29 active tracker observations and initial I2CP TCP

Implementation `b998d3076cd34513ae5459adb7de03553c0b1844`, test-only qualification `82ef47fb32c30f57ebc9e135bcbffb00b4f7660f`: three affected suites and a separate HTTPS/JKS row passed on GCC, Clang and leak-enabled ASan/UBSan. Four private commands passed, including all19 initial-channel cases. Active one-tier numeric UDP current counts follow actual dispatch events; general multi-helper selection remains open. Initial I2CP supports explicit loopback TCP greeting/date observation, cancellation and bounded failure handling, with no accepted session/backend activation. Five initial fixture failures and the later Qt JUnit verifier-only error remain recorded; corrected report validation reused actual successful executions without another compile/test run. Evidence: migration/evidence/20261029-active-trackers-i2cp-channel/verification.json. Current dist replaced the previous copy, which was deleted. Next: signed I2CP session registration and profile-bound original Azureus local value publication. Full P05–P13 remain open.


## 2026-10-29 explicit saved ratings and outgoing I2P datagrams

Qualified source `d384a850980f1b6245230ea852d617b11c405e11`: fresh GCC7/Clang5/leak-enabled ASan+UBSan5 affected suites, private7, both original Java artefacts8send+4status+1payload/reference/direction. One dist replaced/deleted its predecessor. Raw failures and metadata-only corrections remain separate; no replay for report fixes. Explicit one-peer ratings and lease-backed outgoing datagrams remain bounded partial P06/P07 integration. [Evidence](migration/evidence/20261029-saved-ratings-i2cp-datagrams/verification.json). Continue bounded inbound I2P and profile-I/O executor work, then the remaining full plan; no chapter stop or full parity/performance claim.


## 2026-10-29 bounded explicit I2CP datagram receiver

Qualified source `9fca696181291fd9a1dc7d62942c76d23afbba11`: fresh5/5/5 affected suites, private5, actual both-JAR receive/control4cases/reference/direction. One dist promoted, predecessor/testcopies deleted; both proposed registries passed first. Scope remains explicit bounded legacy/fast2/3 datagrams, not real-router/backend/Streaming or delivery parity. [Evidence](migration/evidence/20261029-i2cp-bounded-receiver/verification.json). Continue profile-I/O executor and explicit I2P UDP tracker work, then the rest of the full plan.

## 2026-10-29 bounded profile I/O and explicit I2CP tracker

Source `f9c72f14be1494b6d75b38288f9eff256a1a878b` qualifies bounded profile I/O off the DHT network owner and explicit I2CP hash lookup/connect/scrape. Fresh GCC17/Clang12/leak-enabled ASan+UBSan12 suites, private-runtime17 and actual official/modified Java2signing families/10frames per reference/direction pass. [Review](evidence/20261029-profile-io-i2cp-tracker/REVIEW.md).

One checked dist replaces its predecessor. Both proposed registries pass before promotion; existing states/baselines/dependencies remain unchanged. Real public routing/maintenance, active router/Streaming integration and broader performance gates remain open.

## 2026-10-29 actual isolated-router acceptance

Actual isolated-router acceptance on native `f9c72f14be1494b6d75b38288f9eff256a1a878b` passes warmed Hash38/39 and signed2connect/claimed3scrape/RAW18 through a real Java26 router and external tracker. All3subjects exit0, both clients retire, fences hold and dist remains `0cb123398f3a5462ef6ef1e444f245dd8ce09c757bdc804213fa2f82a50ec976`. Warmed actual signed-LS2 cache is an explicit fixture prerequisite; discovery/tunnels/Streaming/default activation remain open. [Review](evidence/20261029-i2cp-isolated-router/REVIEW.md).
