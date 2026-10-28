# Linux DEB packaging foundation and plugin delivery decision

State: in_progress. Owner/reviewer: Codex, self review only. The packaging
foundation is implemented; an installable native release is not qualified.
User direction on 2026-10-17: Linux first, temporarily bundle Qt, and retain two
GitHub pipeline variants: bundled Qt initially, small system Qt when matching
packages exist. Keep original profile/plugin directories and use GitHub release
hosting for native plugins. See [AD-006](../decisions/AD-006.md) for source/layout.

## Baseline and bounded implementation

Original BiglyBT source commit:
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Inspected catalogue/updater, plugin
scanner/state/uninstaller and profile-location sources are listed in AD-006.
The `.deb` layout is new native packaging, not a port of the Java installer.
The prior C++ runtime implementation remains
`5073f29e81752be5b6cef9ceabddd9e919eaae72`; later packaging implementation and
evidence commits are recorded after the immutable checkpoint exists.

Saved packaging implementation:
`f6e0f2e9fd4ae43c4f375dcf7ec19c9f9a40d534` on the isolated review branch.
Original Java HEAD/index remain unchanged. Bounded self review is
[recorded separately](../updates/distribution.linux_deb/20261017-initial-foundation.md).

New opt-in CPack configuration uses `/usr`, strips ELF files, compresses with one
xz thread, and calculates dependencies strictly. `BIGLYBTC_BUNDLE_QT=ON` adds
private Qt libraries/plugins/translations plus the SDK's ICU support, SPDX records
and notices. Private executables/qt.conf, small launchers and relative RUNPATHs
avoid system Qt/plugin changes. `OFF` packages two executables/notices with system
Qt minimums. Both use the same package name/version with distinct variant filenames.
No full SDK, JAR, profile, maintainer script, service or association is installed.
Native Linux amd64, desktop targets, explicit maintainer and configured Qt version
are prerequisites. Ordinary builds remain unchanged with packaging disabled.

An independent read-only hosted Linux CI job runs the synthetic packaging
contract. A manual provisioned-runner workflow builds the bundled variant and
optionally system Qt, serializing jobs with the same checked-out source revision.
It uploads review artifacts/checksums and never publishes a release. Checkout is
pinned to actions/checkout
`3d3c42e5aac5ba805825da76410c181273ba90b1` (v7.0.1), verified from its primary
release/commit pages. Upload is pinned to actions/upload-artifact
`cf430e030ddbb5b0abf93d22962f4752f3646cd9` (v7.0.2), likewise checked. No remote
CI or publication was triggered. Java CI is unchanged; the checkpoint allowlist
adds only the two specific new native workflows.

## Validation and evidence

- `python3 native/packaging/test_packaging.py`: PASS, six test methods with four
  missing-prerequisite cases and five modified-archive rejection cases. Compiles
  tiny synthetic C++ executables, packages them, confirms automatic libc/C++
  dependencies, explicit Qt minimums, four-file payload, build RPATH removal and
  opt-in behavior. Rejects bundled library, profile, install script, missing Qt
  dependency and retained developer RPATH. Does not launch the executables or
  install packages. An unpackaged external fixture library must fail CPack's
  metadata check. Inspection found CPack automatically adds `--ignore-missing-info`;
  the wrapper removes it, supplies local mappings only for private Qt/ICU, and
  excludes self-dependencies. [Report](../evidence/P02-01-linux-deb/packaging-contract.json).
- `python3 native/packaging/test_qt_bundle.py`: real Qt6.12 runtime package audit,
  extraction/relocation, private loader/plugin paths and offscreen event-loop
  probe PASS without SDK library/plugin environment settings. Tiny application
  fixture only; it never opens a torrent profile or installs a package.
  [Qt bundle report](../evidence/P02-01-linux-deb/qt-bundle.json).
- Actual native desktop/headless CMake configure with pinned GCC16, Qt6.12 and
  packaging enabled: PASS. [Configure command/log](../evidence/P02-01-linux-deb/application-configure.json).
  This exposed and corrected Qt version variable scope across CMake directories;
  no application C++ runtime was changed or rebuilt. Actual bundled configuration
  also PASS: [command/log](../evidence/P02-01-linux-deb/application-bundled-configure.json).
- Read-only `dpkg-shlibdeps` against the retained actual desktop binary: rejects
  missing package metadata for `/opt/Qt/.../libQt6Widgets.so.6`, as expected.
  Before/after executable SHA256 is unchanged. [Preflight](../evidence/P02-01-linux-deb/runtime-metadata-preflight.json).
  This is the system-Qt prerequisite that prompted the user's temporary bundle
  choice, not a successful application package or blocker for private bundling.
- Both local workflow YAML files parse and use read-only contents permission.
  Actual remote execution remains pending; the full build requires a provisioned
  pinned-toolchain/dependency runner, not this running client's machine.

During implementation, the first synthetic run found `/usr/local` instead of
`/usr`; the install-prefix guard corrected it. A malformed negative-test script
initially failed `dpkg-deb`'s permission checks before reaching the auditor; its
fixture mode was corrected and the final negative case exercises the auditor.
Initial temporary reports remain under `/tmp`, separate from the final evidence.

## Remaining gates

Choose supported Debian/Ubuntu releases/architectures and qualified system
Qt6.12/engine packages for the system variant; private Qt is the initial default.
Complete transitive runtime/ABI/license provenance, native application build and
runtime package tests, dependency-install simulation, isolated install/upgrade/
uninstall/rollback and actual UI qualification. No final application size claim
can come from the synthetic fixture's size.

Release owner is `d00rsfan`; five public repository skeletons are created through
authenticated `gh`. Signing and actual code/release publication remain pending.
See [the rollout](../REPOSITORY_ROLLOUT.md). Implement
and test the versioned host ABI, catalogue verifier, original network/update
policy, native plugin discovery/installation, first-run/offline/disabled/missing
states, atomic activation and rollback. Original Java update/uninstall and native
uninstall must preserve the intended shared settings and executable ownership.
No `.so` release, trusted production URL or executable auto-updater exists yet;
the SpeedScheduler pilot is still static. This task does not close P02/P11/F27/F28.
