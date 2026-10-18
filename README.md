# BiglyBTC

Experimental C++23/Qt migration of BiglyBT with the goal of reusing original
configuration, torrent/resume and incomplete download files without conversion.
The complete native application is still under development; this repository
currently publishes the independently buildable native **plugin SDK foundation**
and optional bounded **C++23 core library**.
It is not a replacement client release.

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
