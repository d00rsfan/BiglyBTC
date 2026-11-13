# Optional owned-download Public rating chat notices

Native d043340fb8fc8f195a04bf3489e72530432a786f, tree
8e65cf9e481eaac73cc5f38810382ed454479e20. Original azrating
1629340340d0bc391d9e582f9ee989a93d550581, BiglyBT
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f and azmsgsync
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. Earlier source and exact-method
qualification are in the[notice foundation](../20261113-rating-chat-foundation/REVIEW.md)
and[incoming status](../20261113-chat-incoming-sync/REVIEW.md) reviews.

A scored owned Public publication independently schedules the original optional
notice. The original auto-download-chat setting defaults true. Private torrents
have no automatic download chat even when their Public DDB rating is allowed.
Generic hash reads cannot schedule a notice. Canonical original torrent name
and selected hash identify the room; display renames and tracker URLs are not
used. Existing per-room identity/history are reused, the original per-room
nickname mode is selected, and normal shared Public actors perform discovery
and signed exchange. Opening a background room preserves the user's selection;
explicit join promotes it to an ordinary visible room.

First notices append immediately and save Plugin.azrating.chatState=w only
after a real local ledger commit. Subsequent notices use original30-second sync
polls, a ten-poll maximum and2-minute settle, then skip histories over50 or an
identical own message. Text, trim, origin and timing primitives retain their
separate source qualification. Creation is not a delivery acknowledgement; DDB
outcome and chat outcome remain separate. Changing the rating/auto-chat/source
before a held source job completes cannot authorise a stale notice or saved
mark. Close/cancel invalidates pending work. Current state counters/resume and
unknown fields remain merged through the dedicated saved-state CAS.

Room creation and signature work run after the source job physically releases.
A move-only prepared signed result enters the ledger only under a fresh source
and saved identity check, and binds its unique room lifetime and ledger mutation.
The final review also rejects an expired owner against a moved-from room and
consumes prepared results when a closed/wrong-thread Session refuses commit.
Ordinary existing chat messages keep their original payloads and crypto path.

Explicit finite limits are64notice results/queued intents, four background
rooms, the existing16active/256catalogue rooms, same-hash coalescing, three
conversation-change retries and15-minute inactive background retention. They
are native bounded resource policies, not full original unbounded instance/
queue/TTL equality. Background work is not added to the general catalogue or
selection. Original read-side peek/autonotify, per-download GUI access and its
notification integration remain[separate work](../../tasks/P06-DOWNLOAD-CHAT-ADAPTERS.md).
No full chat UI/non-Public/plugin parity is implied by this finite chapter.

Two generated native clients exchange actual notices through their shared DHT
and MsgSync actors on IPv4 and IPv6. Tests observe remote authors' real signed
messages with origin1, empty per-room nicknames, both scores, persisted w,
unchanged selection and hidden background rooms. Repeat publication enters
synchronisation wait without creating an immediate duplicate. Additional cases
cover disabled/private/generic paths and actual profile-source jobs held while
cancelled, disabled or closed; no identity or w is created after that revocation.
Existing DDB-only fixtures explicitly disable optional auto-chat so their
original Java handler continues to isolate DDB packet behavior; production
configuration defaults are unchanged.

Twelve affected CTests pass in158.72seconds: local-rating host/UI, rooms,
shared owned/generic ratings and Java wire groups, packed/chained/diversified
DDB and late/limits, original notice policy, original incoming status and actual
owned notices. After the final lifetime tightening, the two affected room and
notice groups pass in32.36seconds (1.85/30.49). The static Qt desktop host also
builds successfully. This is GCC16/Qt6.12 host qualification, not a fresh
sanitizer, whole original runtime, platform or real-profile acceptance run.
No live profile, running process, frozen preview/SDK or user-facing image changed.
