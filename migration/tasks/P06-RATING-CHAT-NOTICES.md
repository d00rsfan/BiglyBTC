# Original optional rating chat notices

Next autonomous chapter after bounded shared DDB continuation integration.
Preserve the running frozen preview, SDK and live profile; no intermediate
user-facing executable, new agents or real-room development messages. Group
checks and keep compiler logs local. Commit/push main with exact provenance.

Original baselines: azrating1629340340d0bc391d9e582f9ee989a93d550581;
BiglyBTcb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Source RatingsUpdater.writeRating,
waitForChat/getChatForDownload, BuddyPluginUtils.getChatKey(Torrent),
BuddyPluginBeta message flags/nickname behavior and original chat attributes.

Implement source-qualified notice text/Java trim and own-message dedup policy:
configuration azbuddy.dchat.ui.enable.auto.dl.chat defaults true; persistent
Plugin.azrating.chatState w; subsequent writes wait for sync (30s polls,5min
maximum) then2min and skip busy histories over50 or an identical own message.
First notice sends immediately and records w. Original message flag o=1 and
per-room nickname mode must survive wire/history; preserve unknown attributes.

Integrate genuine source-bound Public per-download rooms as a dependency, using
original canonical torrent name/hash and denying private torrents per original
chat-key policy. Revalidate settings/contribution/membership before effects;
keep DDB and chat outcomes separate. Own/shared nickname handling must avoid
changing unrelated user rooms. Read-side peek/autonotify remains separately
qualified if its discovery prerequisites require another milestone. Do not
claim full per-download chat/UI or non-Public parity from a notice primitive.
Use pure original fixtures and generated owned/Java rooms only.
