# Queue scalar oracle: unused-signature amendment

The subject is the retained, unchanged cb8c6cb9bb97916cbc7e80338a017200ffc3e71f
`RankCalculatorReal.class` and its original nested `SR` class. The actual
constructor, `reloadConfigParams`, `recalcSeedingRank`, `isFirstPriority`,
`getSeedingRank` and `destroy` execute. No copied rank or first-priority formula
appears in the helper, fixture generator, native replay driver or comparator.
No Java/compiler/native execution has been performed by the author.

`QueueRankLauncher` makes an isolated URLClassLoader with helper output first,
the retained original class directory second, and the JDK platform loader as
parent. It verifies the subject's code origin. Only seven original dependency
names are replaced: `COConfigurationManager`, `SystemTime`, `ThreadPool`,
`Debug`, `FileUtil`, `PluginCoreUtils`, and `StringInterner`. Their complete new source is visible
in `source/`. Original subject classes cannot be emitted by helper compilation:
the source path is empty, implicit compilation is disabled, and the runner
checks every output class against the authored source names.

The plugin constructor creates its ordinary inert field containers. Its
`initialize`, `configurationSaved`, timer tasks, `process`, activation requests,
CoreFactory, managers and networking never execute. Configuration registration
and the single unused activation-pool allocation have inert providers. Actual
download/state/stats/scrape interfaces receive strict proxies. All supplied
getter values are explicit fixture inputs. Unknown proxy calls and diagnostic
paths latch a failure even when original code catches an exception. Source
methods contain no host/profile reads. The fence permits class/JDK reads only,
denies filesystem mutation, network/DNS, process creation and thread creation,
and verifies its latched status after invocation. This is an inert-provider
method oracle, not original application/bootstrap/runtime equality.

The retained directory is checked against the complete frozen 5,432-file
retention inventory before and after an attempt. No original Java is rebuilt.
Only new helper sources compile. Every attempt uses a fresh generated `/tmp`
root and a fresh durable result directory; original profile/processes are not
opened. The compiler and actual Java/native replay are parent-owned future
execution, not completed work in this capsule.

## Bootstrap correction and retained first failure

The parent-owned first attempt in `../run-r1` compiled the authored helpers, then
failed during JVM bootstrap with exit -6. Its fatal log identifies
`NotificationThread::initialize` under `Threads::create_vm`; the original helper
launcher and subjects had not run. Those raw logs and the initial eighteen-file
capsule remain untouched. This amendment has not been compiled or executed.

The Java command now uses `-Djava.security.manager=allow`, which permits the JVM
to finish its essential bootstrap before `QueueRankLauncher.main`. The launcher
rejects a preinstalled manager, constructs the unchanged `QueueOracleFence`,
installs it with `System.setSecurityManager`, and checks exact identity before
constructing the isolated helper/subject classloader. The fence remains installed
and verified after original invocation. No original class is loaded by the
system launcher. Its thread, file, network, process and replacement restrictions
remain byte-for-byte unchanged. This is a bootstrap-order correction, not a
relaxation during the oracle subjects or providers.

The runner's helper-output allowlist, before/after retained-cohort checks,
comparison tape, native invocation, timeout cleanup and fresh-attempt archival
are unchanged. New attempts live only under this amendment directory; the
original failed attempt cannot be overwritten.

## Signature-only interner provider and retained fenced failure

The bootstrap-amended parent attempt in `../bootstrap-amendment/run-r1` reached
launcher main with the exact fence installed. Before constructing the first
original rank subject, Java Proxy initialized `StringInterner.FileKey` while
resolving the unchanged `DownloadManager.getSaveLocationFileKey` and
`DownloadManagerState.getFileLink` method signatures. The original
`FileKey.EMPTY_FILE` caused outer `StringInterner` initialization, whose cleaner
starts `AEThread2`; the strict thread fence denied it. Raw original stdout is
empty, so that failed attempt earns no scalar comparison credit. Its complete
logs and both previous frozen capsules remain untouched.

The seventh and only new provider is a signature-only `StringInterner` scaffold.
It retains the `StringSupplier` interface shape and `DirKey extends FileKey`
relationship, with no static EMPTY construction, cleaner, interning, filesystem
value or algorithm. All declared constructors and value methods fail through
the latched `Providers.fail`. The scalar subject has no StringInterner call;
this provides unused return/argument type identities only. Actual original
DownloadManager/DownloadManagerState interfaces are not replaced or narrowed.
The runner additionally requires an explicit eleven-source allowlist, while its
existing helper-output allowlist excludes implicit original compilation. This
capsule has not been compiled or executed; any further unused signature service
initialization must be reviewed explicitly instead of silently stubbed.

## Fixed comparison scope

`vectors.json` and `vectors.tsv` contain 96 explicit input cases, not numeric
expected outputs. Ninety-five predeclared cases admit two scalar comparisons
each: `seedingRank` and `firstPriority`. Inputs cover all five rank modes,
binary32-sensitive ratios, seed fallback, exact ignore boundaries, known zero,
FP ALL/ANY and equality boundaries, integer seed/peer division, idle threshold,
per-download ratio overrides, transient external FP authority, persistence,
stopped/error states, timed boundaries and Java signed-int wrap inputs.

The unknown-scrape case is characterization only: Java's cached/fallback zero
and the conservative native missing-provider policy earn no equality credit.
The comparator never silently removes a mismatching admitted case or field.
Native input rejection/unknown for an admitted scalar is a failure. Explicit
unknown providers remain unknown in the native fixture. No queue actions,
ordering, active flags, grace-state memory, light slots, tags, dynamic probes,
timers, start/stop side effects or engine scheduling receive equality credit.

The controlled original `SystemTime` wall/monotone values are explicit. The
separate `System.currentTimeMillis()` path in timed queued ranking with a
nonzero saved last-active/completed timestamp is deliberately not admitted.
The original constructor's real DownloadImpl-only unwrap is replaced by an
explicit download-proxy/core-proxy binding; constructor integration with a live
DownloadImpl/manager is outside scope. The native driver maps the fixture's
null core PeerManager to explicit zero smoothed contributions, reproducing the
base minimum-time branch; this is no measured global-rate assertion. Current
scrape timestamps precede seeding start, and Downloading inputs precede their
start scrape boundary, so source excludes-self counts equal supplied counts.

## Parent execution recipe

Register/build `queue_rank_driver.cpp` as a small C++23 executable linked to
`biglybtc_core`, using the same strict warnings as other native tests. No extra
Qt/engine/wrapper dependencies are needed. Root chooses target/path; a useful
target name is `biglybtc-queue-rank-oracle-driver`. No CMake file is changed here.

Then, after independent review and explicit execution authorization:

```
python3 build/queue-20261024/oracle/signature-amendment/run.py \
  --driver /ABSOLUTE/ROOT-BUILT/biglybtc-queue-rank-oracle-driver \
  --attempt run-r1
```

The recipe builds only helper Java, executes the actual retained Java methods,
then the actual native pure API on the same input tape, and records raw stdout,
stderr, exact commands, exit codes, driver/class/fixture digests and strict
comparison results. Failed first attempts remain durable. No claimed pass or
observed scalar count exists before that execution.

## Independent implementation-test priorities

1. Scalar first: FP precedence over ignore rules; transient friend/tag FP before
   persistence/completion/stopped checks; tag NOT-FP ordering; FP ANY/ALL with
   disabled time rules; share ratio -1; integer FP seed/peer division; float32
   SPRATIO, PEERCOUNT wrapping and ignore-return precedence. Exact source rank
   results must remain separate from conservative queue-action decisions.
2. Queue limits: forced downloading excluded from original totals, active forced
   seeds counted; zero limits unlimited; minimum downloads and seeding-only
   maxima; waiting/preparing reservations; checking/moving preservation; manual
   stopped/paused never revived; higher-priority queued work stops lower work
   only under the declared policy and safe owner fence. These require native
   deterministic policy/owner tests, not scalar-oracle credit.
3. Stateful activity: exact 30-second force-active and 10-second transition
   boundaries, clock regression rejection, disk-latency recovery, missing rates
   and swarm, startup waits, no spontaneous catchup observations. Use explicit
   host facts and persistent activity memory; no wall-clock sleeps in pure tests.
4. Effect fencing: stale token/revision/hash causes all selected effects to be
   rejected before mutation; stop/removal completes before competing admission;
   failures leave actionable diagnostics and profile state safe; no action after
   close/cancel. Original timers/global/tag-manager effects remain separate.
