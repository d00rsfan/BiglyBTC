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
