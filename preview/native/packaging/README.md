# Linux DEB variants

`BIGLYBTC_BUNDLE_QT` selects two packages built from the same application source:

| Option | Artifact suffix | Runtime delivery |
| --- | --- | --- |
| `ON` (current default) | `_bundled-qt.deb` | Private Qt6.12 libraries, plugins, translations and the SDK's required ICU runtime |
| `OFF` | `_system-qt.deb` | Ubuntu 26.04 system Qt 6.10.2, ICU, MaxMind DB and libarchive |

Both have the same `biglybtc` package name and version. Installing either
variant replaces the previous `biglybtc-experimental` package. To install
only the required Ubuntu packages for the system-Qt variant, run
`sudo apt install --no-install-recommends ./<file>_system-qt.deb`. Add
`qt6-wayland` if you want the native Wayland backend. Ubuntu’s Qt Wayland
package also brings Qt Quick and QML libraries.
The **BiglyBTC** menu entry and a bare `BiglyBTC` command open an existing
`~/.biglybt` profile. Close any running BiglyBT or BiglyBTC instance first.
To choose another directory, run `BiglyBTC --profile /absolute/profile`.
GitHub release builds include the BiglyBTC torrent SDK and DB-IP Country and
ASN data. Both Qt variants can start transfers, Mainline DHT and Public chat.
The **BiglyBTC Preview** entry opens a temporary sample profile. The
packages install no profile files, Java JARs, services, torrent/magnet
associations or maintainer scripts. The C++ runtime and ordinary OS libraries
remain system dependencies.
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
uses package-local metadata for bundled libraries and excludes a self dependency.
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
python3 packaging/audit_deb.py /tmp/biglybtc-deb-output/*.deb --application
```

Replace the example contact. Use a separate fresh build directory and
`-DBIGLYBTC_BUNDLE_QT=OFF` for the system variant. Packaging never installs the
result on the host. The release baseline, exact runtime/source/license closure,
engine package metadata and full application installation tests remain pending.

The Linux packages workflow builds both variants. It checks the package
contents and dependencies, installs both DEBs in separate clean Ubuntu
containers and starts the desktop with a generated profile. The bundled
variant also produces the relocatable archive and checks it after extraction.
Successful pushes to `master` publish three downloads per architecture as a prerelease.
A manual run from `master` can publish by enabling `publish`.

Install one DEB with APT so that it resolves the runtime dependencies.
For the smaller variant, download the file ending in `_system-qt.deb` and
run `sudo apt install ./biglybtc_*_system-qt.deb` from its directory.

History chart Traffic selects Payload, Protocol or DHT for the existing saved UTC
buckets. Each chart keeps its own choice on same-session reopening. Switching
reprojects accepted facts without another read; tables and payload summaries stay unchanged.

Tab to a saved History chart, then use Left/Right, Home or End to inspect exact
UTC buckets, including unknown gaps. The focused cursor and caption show the
selected traffic values. A new accepted chart clears the cursor; no read is added.

Use the focused saved History chart's Copy shortcut (Ctrl+C on Linux), or its
context menu **Copy selected values**, to copy the selected UTC bucket and exact
traffic values, including Unknown. Selection enables the action; replacing the
frame clears it. Hover and navigation do not copy. Only the normal Clipboard is
written, with no new saved reads, file writes or configuration persistence.


Wayland preview diagnostics
---------------------------

Qt 6.12 can repeatedly warn that `disableSurface` was called while `0x0` is
focused when moving focus among non-text widgets. The offline preview launcher
filters only that exact redundant diagnostic. It leaves other focus mismatches,
protocol warnings, stdout, stderr bytes and the child exit status intact, and
keeps Wayland and international text input enabled. This is a console workaround;
it does not patch Qt or establish that unrelated input problems are harmless.
Custom Qt message formats are passed through unchanged.

To inspect every message, run
`BIGLYBTC_WAYLAND_DIAGNOSTICS=1 ./BiglyBTC-preview` from the portable folder.
`QT_FATAL_WARNINGS` also bypasses the filter so an abort diagnostic stays visible.
The direct normal-client entry point is unchanged.

Check the subprocess forwarding and diagnostic bypasses without building Qt/C++:
`python3 -B preview/native/packaging/test_preview.py`.

## Linux CI artifacts

Pushes to `master` build the bundled Qt DEB and the shared-library DEB
on native Ubuntu 26.04 amd64 and arm64 runners. Each package is installed
and started in a clean Ubuntu image for its architecture. CI reconstructs the
daily SDK from pinned upstream sources and verifies the complete source and
installed header inventories. It packages that build's exact shared library.
Publication requires a real peer transfer, Mainline UDP, Public chat message
exchange, per-tag rate tests and startup of the installed engine and chat on
the default profile. The same SDK image is used by the archive.
The package version is the source version plus `~dev.<run_number>`.
Reruns keep the same package version. The job also exports a relocatable
`.tar.gz` with Qt, ICU, the torrent engine and selected runtime libraries,
then unpacks it in a different directory and checks profile reading and the
offscreen desktop on a generated profile.
The host supplies glibc and desktop system libraries. Ubuntu 26.04 is the
tested target and other distributions require compatible system libraries.
Checksums, dependency inventory and the source commit accompany the downloads.
ARM64 uses the bundled native modules. Signed module updates currently target amd64.

The plugin SDK CI
exercises the public C ABI and portable core separately on Windows amd64,
Windows ARM64 and macOS arm64.
The native desktop still relies on Linux profile locking, atomic filesystem
operations and plugin loading. Portable equivalents and runtime tests are
required before packaging a Windows installer or macOS DMG.
