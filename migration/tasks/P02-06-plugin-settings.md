# P02-06 Original external-plugin settings binding

Status: implemented bounded read-only snapshot; qualified across GCC16, Clang24
and ASan/UBSan/LSan. Connect native services to original keys without renaming
folders, IDs or configuration data; no live profile access or automatic startup.

Original core baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
PluginInitializer passes directory.getName as configuration key; PluginInterfaceImpl
prepends `Plugin.`; PluginConfigImpl appends `.`. External getPluginUserFile also
preserves that directory basename beneath the original user profile's plugins
folder (even for a system installation), falling back to runtime ID only when no
plugin directory exists. Loaded-at-startup remains `PluginInfo.<runtime-id>.enabled`.
Do not infer any of these from BiglyBTC repository names or normalize their case.

ConfigurationManager reads Long and narrows to signed int32; booleans test that
narrowed value against zero. Wrong types fall back to supplied/registered defaults
and generate diagnostics. Characterize these methods in a fenced helper with
controlled property/default values, not normal application/plugin initialization.

First consumer is mldht's original defaults/flags/port at wrapper baseline
`3b12515c5541ed75cfb4f795b672d4e786df9f45`. Preserve `IPv4`/`IPv6` spelling in
autostart/autoopen keys. The read-only snapshot must not rewrite malformed/unknown
values, create folders or enable services. Actual lifecycle/network wiring follows.

90 fenced original observations pass on all three compilers; cache/document
regressions also pass. Source/command/binary hashes: P02-06-plugin-settings/matrix.json.
Default BDecoder keys use ISO-8859-1; UTF-8-written non-ASCII keys may not be
findable by the original Java getters. Preserve that behavior and raw bytes.
Actual service lifecycle, host file discovery and editing remain pending.
