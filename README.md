# BiglyBTC

BiglyBTC is a native C++23 and Qt migration of BiglyBT. The desktop and
headless client live in [preview/native](preview/native), the independent
native libraries in [core](core), and the plugin SDK in [plugin-sdk](plugin-sdk).
The original-profile and plugin compatibility work is ongoing.

## Builds

The [Linux packages workflow](https://github.com/d00rsfan/BiglyBTC/actions/workflows/biglybtc-linux-deb.yml)
runs on pushes to `master`, pull requests and manual dispatch. It builds the
Ubuntu 26.04 amd64 DEB with private Qt 6.12, a smaller DEB that uses Ubuntu
shared libraries, and a relocatable Linux archive. Successful builds from
`master` are published as [Linux prereleases](https://github.com/d00rsfan/BiglyBTC/releases).
Each release includes the source revision and download checksums.
See the [packaging guide](preview/native/packaging/README.md) and
[desktop build instructions](preview/README.md).

The [plugin SDK workflow](https://github.com/d00rsfan/BiglyBTC/actions/workflows/plugin-sdk.yml)
checks the public C ABI on Linux, Windows amd64 and macOS arm64. The native
desktop uses Linux filesystem and module-loading APIs. Windows and macOS
desktop packages require platform-specific implementations and validation.

The [plugin catalogue project](https://github.com/d00rsfan/BiglyBTC-plugins)
collects qualified plugin builds and verifies the release catalogue inputs.

## Migration history

The `migration-history` branch retains the full incremental migration
commits, plan, compatibility reports and evidence. The current source branch
keeps the fixtures, dependency records and tools used by the builds.
