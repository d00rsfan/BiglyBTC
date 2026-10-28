# P02-05 Native plugin module foundation and vertical fixtures

User request: implement SDK and azlocprov, then continue other plugins without
waiting for another confirmation. Public source publication is authorized; never
push the mixed Java workspace or live/profile data. Original Java stays running.

Implemented:

- Independently installable C/C++23 SDK CMake package; versioned C ABI and Linux
  owner-thread RAII loader, lifecycle/failed-start/failed-create and descriptor
  contracts; host country-name callback owns locale cache lifetime.
- BiglyBTC-plugin-azlocprov module preserving runtime ID `azlocprov`, original
  numeric Java-version DB preference/fallback, IPv4/IPv6 country lookup, localized
  host names, missing/corrupt DB handling, original loose/JAR flag precedence and
  sizes. Native/Java versions remain separate. No database/assets are redistributed.
- Offline signed fixture store: SHA/size/ABI/platform/ID/version authentication,
  staged immutable artifacts, child startup probe, activation, four actual process
  interruption boundaries, retry and rollback/replay tests. Original bencode
  setting `PluginInfo.azlocprov.enabled=0`, opaque bytes and JAR selection survive
  Java/native/Java pure loader checks with unchanged bytes/inodes.
- SpeedScheduler independent shared module wraps its existing original-compatible
  reducer. All6336 original command/rate/ownership states replay through the ABI;
  five existing policy/XML/ownership suites remain separate. No actual engine
  commands, ownership-file writer, AutoSpeed/tag router or editor is enabled.
- mlDHT bind/effective-state policy port, plus4860 exact extracted Java-method
  observations. Explicit host values replace NetworkAdmin/core-manager services;
  this is not a full plugin/engine operational oracle or working DHT network.

Baseline originals: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, azlocprov
`8e8224030ac772fcc8fcb22e80954342d6a66be1`, SpeedScheduler
`1ee6a53dae017696d2f0002e4a0d74f5a9476af5`, mlDHT wrapper
`3b12515c5541ed75cfb4f795b672d4e786df9f45`. Mainline engine persistence and engine
pairing remain outside this policy slice. None of these whole plugins is verified.

Evidence and reproduction: `migration/tools/plugin_matrix.py`,
`migration/evidence/P02-05-plugins`, `migration/tools/mldht_policy_oracle.py` and
`migration/fixtures/mldht-policies`. Each final compiler capsule pins exact source,
binary and command/log hashes. Earlier pre-version-binding and pre-locale-host
runs are retained with their actual failures; do not count them as final passes.
No broad application44-test rerun is implied by a scoped plugin matrix.

Remaining: original GeoIP2 Java dependency/shipped-artifact closure, broader locale
and Java properties/version edge cases, native host discovery/load/unload UI and
session integration, production installer/updater/trust/transport/dependency and
licensing closure, complete azlocprov tag examples, full remaining plugin ports.
The generated-profile development gate remains; do not point these tools at the
user's live `.biglybt` directory or enable an automatic download.
