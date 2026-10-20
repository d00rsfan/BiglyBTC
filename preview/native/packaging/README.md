# Linux DEB variants

Current development priority (2026-10-18): run the first UI preview directly from
`dist/BiglyBTC-preview`; see [launch and walkthrough](../../migration/PREVIEW.md).
Further DEB/release work is deferred until desktop feedback. A local bundled-Qt
package and user-only install were already produced; no public release is implied.

Linux amd64 is the first delivery target. Windows/macOS qualification is deferred.
`BIGLYBTC_BUNDLE_QT` selects two packages built from the same application source:

| Option | Artifact suffix | Runtime delivery |
| --- | --- | --- |
| `ON` (current default) | `_bundled-qt.deb` | Private Qt6.12 libraries, plugins, translations and the SDK's required ICU runtime |
| `OFF` | `_system-qt.deb` | Small application package; system Qt6.12/QPA/Wayland packages |

Both have the same `biglybtc-experimental` package name/version, so they are
alternative builds of one application, not two clients to install concurrently.
The generated-profile restriction remains intact. Both include an offline sample
launcher and a **BiglyBTC Preview** menu entry. Neither installs profile files,
Java JARs, services, torrent/magnet associations or maintainer scripts. The
torrent engine, C++ runtime and ordinary OS libraries remain system dependencies.
No full SDK, development headers or build tools are bundled.

The bundled variant installs executables under `/usr/lib/biglybtc/bin` and uses
tiny launchers under `/usr/bin`. A private `qt.conf` and relative ELF RUNPATHs
resolve libraries/plugins inside `/usr/lib/biglybtc/qt`; system Qt and other
applications' plugin paths are not changed. The planner follows Qt/ICU SONAME
dependencies and copies an explicit desktop-plugin set plus translations,
licenses and the original SDK SPDX records. It rejects unexpected SDK RPATHs.
This follows Qt's [private deployment model](https://doc.qt.io/qt-6/linux-deployment.html).

CPack strips ELF files and uses single-threaded xz compression. Its normal
`--ignore-missing-info` behavior is overridden: a strict `dpkg-shlibdeps` wrapper
uses package-local metadata only for bundled Qt/ICU and excludes a self dependency.
All external runtimes still require actual Debian metadata. Missing metadata
fails the package build. See the [CPack DEB documentation](https://cmake.org/cmake/help/v4.2/cpack_gen/deb.html).

From `native/`, on a provisioned build host:

```sh
cmake --preset desktop-gcc -DBIGLYBTC_DEB=ON -DBIGLYBTC_BUNDLE_QT=ON \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBIGLYBTC_DEB_CONTACT='Actual Maintainer <actual-email@example.org>'
cmake --build --preset desktop-gcc --target BiglyBTC BiglyBTC-headless
cpack --config /tmp/biglybtc-desktop-gcc/CPackConfig.cmake \
  -B /tmp/biglybtc-deb-output
python3 packaging/audit_deb.py /tmp/biglybtc-deb-output/*.deb
```

Replace the example contact. Use a separate fresh build directory and
`-DBIGLYBTC_BUNDLE_QT=OFF` for the system variant. Packaging never installs the
result on the host. The release baseline, exact runtime/source/license closure,
engine package metadata and full application installation tests remain pending.

The hosted contract workflow runs `python3 native/packaging/test_packaging.py`:
synthetic executables check payloads, RPATH removal, dependencies, prerequisites,
tampered archives and rejection of an unpackaged external runtime. Nothing is
installed or launched by that test. `python3 native/packaging/test_qt_bundle.py`
additionally packages a tiny real Qt application, extracts it to a fresh owned
directory, checks loader resolution, and runs its short offscreen event loop
without SDK library/plugin environment paths. This is Qt relocation evidence,
not full application or real compositor qualification.

`.github/workflows/biglybtc-linux-deb.yml` is a manual build/artifact workflow.
It selects bundled Qt and optionally adds system Qt using `build_system_qt`.
The saved draft now targets GitHub-hosted `ubuntu-26.04`, provisions exact
compiler/core dependencies with the hosted bootstrap, and records the other
installed versions and downloaded Qt archives. Each variant has a separate
build directory; builds are serialized. Exact Qt6.12 Debian packages are a
prerequisite for the system variant; this host initially offers Qt6.10.2.
Full public application source export and an actual hosted app build remain
pending. No publication is enabled; development signing is still required before
a release. Workflow syntax checks do not establish a successful hosted build.

Plugin GitHub release/catalogue and shared-profile contracts are in
[AD-006](../../migration/decisions/AD-006.md). The native plugin loader/update
verifier remains pending; no automatic executable plugin download is active.

History chart Traffic selects Payload, Protocol or DHT for the existing saved UTC
buckets. Each chart keeps its own choice on same-session reopening. Switching
reprojects accepted facts without another read; tables and payload summaries stay unchanged.

Tab to a saved History chart, then use Left/Right, Home or End to inspect exact
UTC buckets, including unknown gaps. The focused cursor and caption show the
selected traffic values. A new accepted chart clears the cursor; no read is added.
