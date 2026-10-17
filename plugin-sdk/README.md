# BiglyBTC native plugin SDK

Experimental Linux LP64 C ABI with an independently installable CMake package.
The host is C++23; plugin interfaces contain fixed-width C fields, borrowed
pointers and caller-owned buffers. No Qt or C++ standard-library objects cross
the module boundary. The interface is version1 for development, not a production
ABI stability promise.

```sh
cmake -S plugin-sdk -B /tmp/biglybtc-sdk-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/biglybtc-sdk
cmake --build /tmp/biglybtc-sdk-build -j2
ctest --test-dir /tmp/biglybtc-sdk-build --output-on-failure
cmake --install /tmp/biglybtc-sdk-build
```

Consumers use `find_package(BiglyBTCPluginSDK 0.1.0 EXACT CONFIG REQUIRED)` with
the install prefix in `CMAKE_PREFIX_PATH`, then `BiglyBTC::PluginAPI` for modules
or `BiglyBTC::PluginHost` for the Linux loader. Only
`biglybtc_plugin_entry(uint32_t)` is required to be exported. Return null for
unknown ABI versions. Original runtime IDs and shared settings remain unchanged.

One host owner serializes every lifecycle and operation call. Callbacks cannot
re-enter/unload the module. A host country-name callback owns locale cache
lifetime; the location module can fall back to database English without one.
Stop drains work; destroy also handles failed start/create; all plugin instances
must be destroyed before the library closes. The loader retains the selected
artifact inode and verifies ID/version/capabilities before starting it.

Authenticate code **before** calling the loader: loading a native library executes
trusted code and is not a sandbox. Never modify a loaded artifact in place. SDK
tests cover C structure layout, lifecycle, failed creation/start, descriptor
rejection, move ownership and unsafe file modes/symlinks.

`tools/plugin_store.py` is an offline development prototype, accepting only
generated `/tmp/biglybtc-*` profiles, explicit trust keys and child activation
probes. It stages immutable versions in an existing original plugin folder's
`biglybtc/linux-amd64/1/<version>/` child, preserves Java files/settings, and
supports signed activation, interrupted retry, rollback and native uninstall.
It is not an enabled app updater. Production still needs complete host lease and
descriptor traversal integration, transport policy, signed catalogue expiry,
key rotation, dependencies and release qualification. Generate test keys only
under disposable test directories; never commit private signing keys.

The experimental code is GPL-2.0-or-later. See the accompanying LICENSE.
