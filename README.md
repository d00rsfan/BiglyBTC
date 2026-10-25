# BiglyBTC

Experimental C++23/Qt migration of BiglyBT with the goal of reusing original
configuration, torrent/resume and incomplete download files without conversion.
The complete native application is still under development; this repository
publishes the independently buildable native **plugin SDK foundation**,
optional bounded **C++23 core library**, and the first **classic desktop preview**
in [preview/](preview).
Full client behavior and seamless original-profile switching remain under development.

The classic preview has separate incomplete/complete tables, a name filter and
shared General/Files details. Its default launcher uses generated sample data
with transfers disabled. See [build instructions](preview/README.md),
[verified milestone](milestones/classic-preview-20261018.json) and
[hosted desktop checks](https://github.com/d00rsfan/BiglyBTC/actions/workflows/native-preview.yml).
The existing root SDK build remains the default; build the desktop from
`preview/native`. Keep local build outputs under `build/desktop`.

Statistics (Ctrl+5) includes retained session Activity, current peer traffic,
compatible overall totals/marks and minute recording for source-qualified local
sessions. Native Cache/Tracker/Counters/Connections tables consume actual engine
measurements. Offline samples stay read-only; full Statistics and original-client
service parity remain under development. See [preview details](preview/README.md).

The next core chapter now retains multiple torrents under one worker-owned
session, with shared Start/Stop/Pause/Resume/Queue actions, compatible saved
intent/checkpoints, journaled `.torrent` import and stopped-file priorities.
The explicit `--profile /absolute/profile` entry uses the original profile lock;
qualification uses generated profiles and owned loopback peers. Magnet
resolution, actual seeding/handoff checks and the combined compiler/runtime
boundary remain in progress. See [core milestone evidence](migration/evidence/torrent-core-20261024/milestone.json).

The source application and its running profile are kept separate from development
tests. Native plugin repositories retain their original runtime IDs. Native
artifacts belong in an original plugin folder's `biglybtc/` child beside Java JARs;
Java descriptors/settings and JAR versions remain intact.

See [plugin-sdk](plugin-sdk) for CMake build/install commands, the experimental
C ABI, lifecycle rules and generated-profile offline installation prototype.
Country lookup and SpeedScheduler have scoped local module tests; full plugin,
UI/session, compatibility and release qualification remain ongoing.

The core package supplies bounded bencode/document, saved-library/cache, torrent
geometry/file policy and executor components. It contains no torrent engine,
profile writer, desktop or running plugin host. Build it independently with
`cmake -S core -B /tmp/biglybtc-core-build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/g++-16 -DCMAKE_INSTALL_PREFIX=/tmp/biglybtc-core-install`,
then `nice -n 5 cmake --build /tmp/biglybtc-core-build -j2` and
`cmake --install /tmp/biglybtc-core-build`. Native consumers use
`find_package(BiglyBTCCore 0.1.0 EXACT CONFIG REQUIRED)` and `BiglyBTC::Core`.
The root option `BIGLYBTC_BUILD_CORE=ON` also enables it; the default root build
continues to build only the C ABI SDK. Compiler/library snapshots are pinned by
core/cmake/Toolchain.cmake. Static core consumers must use the same qualified
C++ toolchain and build options; this is not a stable public C++ ABI.

Production development uses C++23, GCC16/Clang24 and Qt6.12. The SDK CI recipe
targets GitHub's hosted GCC14 toolchain as an additional standalone C boundary
check. The SDK workflow is registered under `.github/workflows/`; see Actions
for current run results. No stable
plugin ABI, signed production catalogue, public binary release or complete
performance/behavior equivalence is claimed by this source foundation.

## Continuous integration

The workflow is [`.github/workflows/plugin-sdk.yml`](.github/workflows/plugin-sdk.yml). [View runs](https://github.com/d00rsfan/BiglyBTC/actions).

It runs on GitHub-hosted Ubuntu 24.04 runners for pushes to `main`, pull requests, and manual dispatch.

For future workflow changes, push using this repository's SSH URL (`git@github.com:d00rsfan/BiglyBTC.git`). HTTPS pushes authenticated with an OAuth token require the additional `workflow` scope to add or update workflow files. Viewing public Actions runs does not require that scope.

The connected Statistics accounting milestone is qualified at source
`292193b3e837d6bf36468d6c64ee8c982a693003`; see the [validation record](migration/evidence/statistics-accounting-20261022.json).
The remaining Statistics services and full client migration are still in progress.

Manual Tags now reads original tag.config and displays observed per-torrent rates.
Activity offers original-style ten-second windows, measured intervals and configured
combined smoothing. See the [qualified Tags/Activity milestone](migration/evidence/statistics-tags-activity-20261023.json).
Full Statistics, automatic tag management and remaining services are still in progress.

Statistics now includes Country, Cache, Tracker, saved Capacity/ASN, DHT, DDB
Operations and Vivaldi views alongside Activity, Transfers, Counters and Tags.
The [qualified native families record](migration/evidence/statistics-families-20261023.json)
tracks the current portable preview and remaining service integration.

Application-owned DHT observations now reach Statistics independently of the torrent
engine, with retained router counts and joined shutdown. See the
[qualified owner/router milestone](migration/evidence/statistics-owned-dht-20261023.json).

Transfers → Capacity now plots the original saved SpeedManager map, preserving
malformed gaps and raw values. A bounded passive mapper reproduces source
variance/limit-estimate arithmetic; it does not start probes or adjust limits.

Capacity also receives actual owned-session rate windows and correlated owned
PING observations, with separate live/saved maps, retained states and source
sustained/choke policy candidates. Viewing Statistics starts no probes or limit
adjustments. These native session inputs include local traffic; full original
global NoLAN/automatic-speed service parity remains in progress.

The [qualified SpeedManager record](migration/evidence/statistics-speed-manager-20261024.json)
includes local compiler/sanitizer checks, hosted130 units/13 smoke rows and the
updated private-Qt portable preview with complete rollback.

Actual owned DHT UDP payload bytes now feed Overall and minute history, preserving
unknown gaps and independently joined final prefixes. Vivaldi displays the
bounded set of actual peer reports with RTT and provenance; public DHT routing
and whole original-service parity remain in progress.


Statistics DDB Operations now includes an observed recursive GET tree and
read-only inspection of actual returned values. Explicit owned lookups follow
discovered contacts only after exact endpoint admission, retain actual request
and cancellation outcomes, and publish immutable snapshots. Opening Statistics
does not start a lookup. Public DHT routing, PUT and other engine migration work
remain separate from this owned-service implementation.

Connections and Tracker include bounded histories from actual accepted engine
observations. Current transfers also show checked protocol-traffic shares.
Missing observations remain gaps, and closed sessions retain last-observed
facts. Statistics reuses existing sampling and does not start network services.

Everyday My Torrents now supports concurrent retained downloads/seeding, saved
Start/Stop/Pause/Resume/Force Start/queue controls, stopped-file priorities and
compatible torrent/magnet intake. Pending original magnets can receive an explicit
destination. The portable build offers `./BiglyBTC --profile ABSOLUTE_PROFILE`;
its separate offline preview still generates sample data. The bounded implementation
and failure/repair evidence are in [the workflow record](migration/evidence/torrent-workflows-20261024/README.md).
Full original engine/plugin parity and public-swarm performance remain in progress.


Bounded compatibility-provider followup (2026-10-25): implementation `c8afff5311e4ff650b17d09651e3db440a0c93e8`, test-only qualified source `471ebb75fc66fefb0208b0d086a934b68aced84b`. Ordinary queue/file-priority/probe/rate facts, sequential bulk file operations, local rating editing, nonlinear-tail corners and optional combined counters are qualified in [the current record](migration/evidence/20261025-compatibility-providers/completion.json). Effective40 local suites per compiler are a38+2 results union. The b0e runtime candidate remains private-only/unpromoted; no new causal/full-CF claim. Verified native SOCKS4a/USERID and imported Scheduler recovery are the two pending milestones in this same chapter, then STOP.


The two authorized final milestones are qualified: verified native SOCKS4a/USERID source `b43971447b44db2f107b205204e1637bff8124ff` and imported-original Scheduler source `6b44a2c21f88c32d6f94fe595d4a42877a895bc1`, final qualified source `861d4f17f2454086de8ec44a9814c292ce1f41fe`. Each GCC/Clang/leak-enabled ASan-UBSan lane passes20 affected suites; actual generated Java/native/Java pause switchback passes2 cases, and the standalone old-SDK denial proof covers275 headers without LT/loader linkage. These are bounded numeric SOCKS framing/authentication and explicit imported-pause recovery, not whole network/Scheduler parity. The current milestone plus two authorized milestones are finished: STOP. No next chapter or autonomous additional work. All CF01–CF08 remain open; no tracker/app-hostname4a proof, Java-enforced journal barrier, crash-recovery UI, new causal/DHT/public-wire, full network or performance closure is claimed.
See [the final two-milestone record](migration/evidence/20261025-proxy-scheduler/verification.json).
