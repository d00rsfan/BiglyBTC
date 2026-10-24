# Source attribution and bounded export

This native C++23 implementation follows the Speed Scheduler plugin from
[BiglySoftware/BiglyBT-plugin-SpeedScheduler](https://github.com/BiglySoftware/BiglyBT-plugin-SpeedScheduler),
commit `1ee6a53dae017696d2f0002e4a0d74f5a9476af5`. Its runtime identifier remains
`SpeedScheduler`, and the display name remains `Speed Scheduler`.

The native scheduler policy, XML reader, pause-ownership byte codec and module
boundary preserve the existing source references and comments. The exported
`LICENSE` is the BiglyBTC native-work GPL-2.0-or-later text. The inspected original
plugin tree has no top-level license file; this notice records attribution and
does not invent an upstream license grant. No original Java source tree, JAR,
javadoc, images or user configuration is included in this native source export.

The generated XML/time, tick and pause-ownership fixtures retain their original
manifests, hashes and expected bytes. Original observations use the plugin pin
above and, where required, BiglyBT commit
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Ownership fixtures explicitly distinguish
the native rejection of an unterminated foreign last line from Java's merging
behavior. The same tick observations are replayed through the policy and the
shared-module boundary; these are two paths, not twice as many original cases.
Qt Core is a separate build dependency used by the XML reader.

`SOURCE_PROVENANCE.json` in an export identifies the actual immutable native
review commit, separately pinned public SDK commit and exported file hashes.
Hosted CI replays frozen observations without a JVM or ordinary client profile.
The module returns rate/torrent commands in caller-owned buffers; it does not
apply engine commands, write pause files, run timers, discover peers or enable
itself in a client. Source publication and CI do not establish a complete plugin
or binary release.
