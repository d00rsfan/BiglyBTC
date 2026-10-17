# BiglyBTC

Experimental C++23/Qt migration of BiglyBT with the goal of reusing original
configuration, torrent/resume and incomplete download files without conversion.
The complete native application is still under development; this repository
currently publishes the independently buildable native **plugin SDK foundation**.
It is not a replacement client release.

The source application and its running profile are kept separate from development
tests. Native plugin repositories retain their original runtime IDs. Native
artifacts belong in an original plugin folder's `biglybtc/` child beside Java JARs;
Java descriptors/settings and JAR versions remain intact.

See [plugin-sdk](plugin-sdk) for CMake build/install commands, the experimental
C ABI, lifecycle rules and generated-profile offline installation prototype.
Country lookup and SpeedScheduler have scoped local module tests; full plugin,
UI/session, compatibility and release qualification remain ongoing.

Production development uses C++23, GCC16/Clang24 and Qt6.12. The SDK CI recipe
targets GitHub's hosted GCC14 toolchain as an additional standalone C boundary
check. Recipes are currently under `ci/`, awaiting workflow-scope authorization
before activation; no hosted CI pass is claimed. No stable
plugin ABI, signed production catalogue, public binary release or complete
performance/behavior equivalence is claimed by this source foundation.
