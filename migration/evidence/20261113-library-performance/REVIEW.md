# Library startup: qualified performance milestones

Work continues under the user's autonomous instruction. The running preview,
its executable and profile have not been replaced or written by development
checks. This record describes generated-fixture acceptance, not a completed
real-profile round trip or full Java performance parity.

## Immutable sources

Original BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
ConfigurationManager.java, DownloadManagerStateImpl.java and GlobalManagerImpl.java
under core/src/com/biglybt/core/{config,download,global}/impl. Their retained
configuration/global-state and loadDownloads behaviour is the compatibility
reference. The native runtime qualification machinery has no claimed Java
implementation equivalent; its previous native baseline is recorded below.

- cache: `2ecbaa28bcdb0475ace5fd87dfbd8a2709ad6d71`, tree `f57a1b5c8a206b02b85490b9cef3f27fd8cffd7b`.
- metadata: `65ab4410c9468dfe290b672c448fd61a16c1518d`, tree `a1def7e998db0225eb956aa2dccf56903e42501c`.
- mapping: `32bf1eb9b6e4142abc8fa2b109a67a8c898258b4`, tree `815d9397f1c0773e812dba1b4dd5d75b0406582c`.
- continuation: `10b37f78c588d638f5cb162c25a5e71388fa6ab9`, tree `b340accc52ed0b3a39f54f20fcba84693799d588`.

The source-qualified libtorrent 2.0.12 SDK is unchanged; full dependency hashes
are in verification.json. Native core ABI consumers were rebuilt coherently.
No separate human or sub-agent review was performed.

## Changes and safety

Immutable bencode documents share their backing trees; actual edits detach.
Config loads still read fresh bytes and compare the entire preimage before a
cached tree is used. All parser limits, recovery priorities, profile lease/OFD
ownership and write CAS checks remain active. Typed plain/gzip snapshots share
an eight-entry, 64 MiB budget. Gzip CRC/inflation and tree quotas remain enforced.
This is neither an mtime cache nor a new source of filesystem authority.

Loaded-provider qualification reads one complete bounded mapping snapshot
before and one after validation of all required exports, rather than reading
/proc/self/maps separately for every symbol. Dlsym/default export identity,
dladdr, executable PT_LOAD ranges, canonical paths, open-file identity, full
SHA256, before/after metadata, mappings and ABI checks are retained. Diagnostic
timing is opt-in and cannot grant approval.

Startup continues in bounded worker turns without a one-second pause after
every batch. Full background maintenance/presentation keeps its one-second
cadence. Commands, cancellation, close, recovery checks and original queue /
per-disk checking policies retain the same serial owner. Pending progress is
published through maintenance and once more at final completion.

## Measured results and checks

The generated 128-torrent Host fixture uses two 8 MiB pieces per partial
sparse file plus 64 KiB opaque inventory data per row. It has no DHT, tracker
or Internet dependency. Maps remained valid before/after joined close with
certified storage release and no checkpoint/accounting failure.

- config and gzip cache: admission 42569 ms; including close 49990 ms.
- batched mapping snapshots: admission 31227 ms; including close 40759 ms.
- bounded startup continuation: admission 11071 ms; including close 20058 ms.

These are single stage-to-stage diagnostic runs on one host, not statistical
benchmarks, Java comparisons or an extrapolation to the real library. The
separate decode-only versus fresh cached-read benchmark is included with exact
counts in verification.json and measures that stage only. Raw build logs,
profiles, perf samples and user-specific diagnostics stay outside public Git.

Qualification covers 15 cache families/660 assertions, 61 managed-storage
families/4776 assertions, 70 portable checks plus 20000 malformed inputs,
13 settings families/522 assertions, 17 resume/Pause/recheck QTests, 16 merge
QTests, 10 mapping families/143 assertions, four owned I/O fault refusals and
positive binding. Additional startup/force/stop/early-close and manual recheck
runs passed 7 and 5 QTests respectively. This is Release qualification, not a
sanitizer run. Tests used generated profiles and owned loopback endpoints.

## Qualified API lifetime follow-up

Native `91ace0b2a3f82e5b39b34dfd116a2fdb099e5c05`, tree `33c830543777b50e3bcdb4ca970c109e0cff1ded`, retains immutable SDK APIs per actual engine owner,
following the existing mainlinePortApi lifetime. The full source/artifact/export/
mapping/descriptor/digest proof runs before the first corresponding use; the
same proven DSO remains pinned until callbacks and SDK join are finished. A new
engine owner requalifies. There is no process-global cache, timestamp digest
cache, caller-supplied approval object or weakening inside the runtime guard.
Each torrent still gets independent payload/reset counters, tracker bindings,
observer ring, profile/key checks and policy decisions.

With this follow-up the same 128-row fixture admitted in 4305 ms, total11586 ms
including close, with three complete qualifications across the entire owner.
Full payload/reset suite7, selected-swarm suite25, actual IPv4/IPv6 tracker
transfer/Unicode/ampersand/control suite11, observer-bounds suite3 and all four
owned binding-negative cases passed. The first selected-swarm run exposed11
stale test expectations for older subset providers. The current full SDK
cohorts correctly reject missing/foreign mandatory V2/V3 exports; updated tests
assert those refusals, zero foreign calls, balanced pins and preserved sources.
Product qualification was not relaxed to satisfy those older expectations.

## Remaining work

Sustained passive-poll costs remain under investigation. The qualified candidate has not replaced the user's
running image. Actual profile comparison is deferred until normal Exit, using
the already-held compact private baseline. Selected-v2 hybrid warm resume and
broader advanced engine/lightweight seeding work remain explicit follow-ups.
Whole migration and the performance chapter stay in progress.
