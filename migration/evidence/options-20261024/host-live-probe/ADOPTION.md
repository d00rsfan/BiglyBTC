# Actual Options Host live probe

Source-only standalone QApplication main; no Q_OBJECT/moc, source/native edits, builds or execution by the author. Native source reference: c191e73cb0feffbd4a61f1bef67df7f5dcb8d621. Source SHA256: 0075e6ff83042cf4e7ff3316023ea340dba9d965d89cf94c05cac9e3187f0232.

After the active Clang/sanitizer compilers finish, root may compile ONLY this translation unit once using the existing bound-gcc options-host target's compiler definitions/includes/options, then link with that target's cached link command/libraries/RPATH. Replace its options_host_tests.cpp.o with this new object and choose a new output executable; do not link the original test main. The target's autogen mocs_compilation object is harmless if retained; the probe needs no new moc generation. No CMake/shared source change or original core rebuild is needed. Approved runtime binding is mandatory; the default/unbound fallback cannot produce the required actual idle observation and is a failure, never a skip.

Example run after root compilation, with a fresh absent output root:

    QT_QPA_PLATFORM=offscreen /absolute/owned/options-host-live-probe /tmp/biglybtc-options-host-live-20261024-r1

The probe creates that private 0700 root/profile and keeps generated data on success/failure. It writes result.json only after all assertions pass; failure.json plus stderr on failure. It opens only the generated empty profile and an actual bound numeric 127.0.0.1 TCP listener used as a nonzero owned-port startup token. There is no torrent, seed, tracker, public endpoint, peer connection, metadata download or original Java/client/profile access. The dummy listener must have no accepted connection. long.term.stats.enable=0 prevents minute history; explicit joined close may save genuine Overall statistics in the generated profile.

Checks use actual Bridge load/save signals and actual nativeSessionReady packets: same applicationRun/epoch through live 17/19 → 23/29 KiB/s updates, actual get_settings-derived rate values and actual active proxy Disabled. A saved pending 127.0.0.2 bind/port plus matching SOCKS5 tracker/peer proxy stays pending restart; a subsequent queue-only save and read preserve the cumulative restart requirement. Queue caps are checked in committed/reloaded Options facts. The Bridge packet does not expose get_settings queue caps/listen interfaces, so those are deliberately NOT presented as actual active readback proof. Existing independent owner tests cover those fields.

A literal opaque original field is byte-preserved on every successful sparse save. An explicit external change to that field invalidates the retained config CAS; a requested 99 KiB/s upload save must fail, preserve all external config bytes and leave fresh actual engine rates at 23/29 KiB/s with proxy Disabled. Empty inventory and absent payload/torrent/history directories remain checked. Final real Closed observation, joined/storage-released acknowledgement and same-inode lease reacquisition are mandatory.

All waits are bounded event processing with a 1 ms scheduling yield; no time/sample is invented and no sleeps manufacture evidence. Optional services/factories remain absent. Scope is this generated bound Host→engine branch, not public proxy reachability, arbitrary settings combinations, scheduler policy parity or a new CI matrix.

Read-only native input pins at author freeze:

```json
{
  "preview/native/app/session_bridge.hpp": "f67ec822aff525de53f6bc4f6780e19b732a59eaf1e12ae35bf804dd59442df8",
  "preview/native/app/session_bridge.cpp": "ef970eda8be83bec0c01abfa3ac26566bc51b2880dff600eaf0da50ed855d212",
  "preview/native/ui/options_presentation.hpp": "f216ab046f6d3cec1af2ffe912ca071614e2dfb2f1de10b005cad1f5264a74c0",
  "preview/native/ui/native_session_presentation.hpp": "59296d5a0c70cfa14a486b06d1c9f0d2f688cd63394582c6060a03f5974f6e4b",
  "preview/native/engine/persistent_session.cpp": "5839a934418a474f30961d51b883103fed7ee420d36cb66a90bea78a815f1ff4",
  "preview/native/engine/live_settings.cpp": "dbf769390a53a6206b672de2ca1f1472d8afecec33f9e66df9eaa7154e2b6544"
}
```
