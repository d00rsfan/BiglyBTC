# Runtime chat views and original window preferences

Native `0a50f20190e03dccd78f9b8ecaba822acb3d636e`, tree `e7de3a072abc1aa8b5b9d5d224a729a68b534982`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
BuddyPluginBeta private getClone/destroy and BuddyPluginViewBetaChat
AC:KeepAlive/createChatWindow. Private clones retain the same chat instance;
Keep alive holds an extra runtime clone. It is independent of saved favourites.
The original standalone flag controls the inverse on-top popout option;
windows-to-sidebar applies unless popout is forced.

A bounded view-reference model retains facade ownership until its first view
adopts it. Duplicate IDs are idempotent; unknown releases cannot underflow;
last private view delegates to the existing signed quit/linger unless an extra
Keep alive reference remains. Facade requests bind service generation, actual
room epoch and view ID. Public catalogue ownership remains unchanged.

Concurrent immutable room transcripts are available for watched views, keyed by
actual room epoch. Caches are computed only for selected/download/watched rooms,
are shared by snapshots and invalidated across rejoin/identity replacement.
Old view IDs and Keep alive do not attach to a new room epoch. The selected
room's transcript stays independent of another watched room.

Original azbuddy.dchat.ui.standalone.windows and
azbuddy.dchat.ui.windows.to.sidebar defaults/updates pass through the profile
broker with fresh compare-and-swap, retaining unrelated fields. No private chat
key/history/favourite is saved by runtime view or Keep alive operations.

Tests passed: private clone/last-view/Keep alive/bounds0.01s; exact profile keys,
fresh/stale CAS and unrelated-field preservation0.08s; actual SDK owner with
independent watched/selected transcripts, stale generation/epoch refusal and
empty transcript after rejoin0.48s. One grouped build took223.02s; no per-edit
full suite was run. Same frozen SDK/GCC16/C++23/Qt6.12. No UI window consumer or
private connecting placeholder is enabled by this bounded foundation alone.

Next implement controller per-view projection and fixed-room popout/tab registry,
read focus, Close/Keep alive, connecting/cancel and geometry. Whole private-window
chapter remains open. The delivered tracker/NAT preview and running process are
unchanged; no new image, profile write, real peer contact or agent was used.
