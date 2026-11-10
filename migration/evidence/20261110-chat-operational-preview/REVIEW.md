# General Public chat preview

The independent room worker, original profile identity/history handling, shared
SDK UDP transport and classic UI now form a usable general Public-room preview.
Open Community → Public Chat Rooms without selecting or starting a torrent.
Join/leave, signed messages, saved favourites, shared/per-room nicknames, history,
unread indicators and recent-message identities are connected. Enter sends;
Shift+Enter inserts a newline. Read-only managed rooms require the owner signature.
See [usage and scope](../../../preview/native/CHAT_ROOMS.md).

The final nine targeted CTest suites passed (39.44 s), followed by the final
keyboard/UI suite (0.93 s). A [synthetic screenshot](UI.png) was inspected; no real profile
or room content appears in it. Generated discovery clients use their actual SDK
listeners and an owned Azureus seed to obtain membership, acquire fresh tokens,
publish and exchange signed messages, then restore encrypted history after join.
A second end-to-end case follows a DHT diversification redirect to a derived key
and discovers a peer there without direct peer injection or a root STORE.

Five unmodified source-pinned original azmsgsync classes on JDK 17 verify actual
handler message creation, synchronization and Native replies in both directions
through pipes. The original DHT storage helper confirms derived discovery keys.
The exact original Buddy sorting methods agree on 389 generated branch/skew/tie/
cycle cases. 512 generated signing identities exercise bounded 128-message/key
catch-up; this is not 512 JVMs or a 512-socket Public room. Original conversation
previous-message/sequence selection and history continuation are preserved.
The [opt-in reference](../../../preview/native/tests/oracle/msgsync/full-handler/README.md)
keeps sockets, children and personal files fenced out.

Real Public NODE-only probes used fresh temporary profiles and random ports.
Both families produced correlated address reports and actual NODE replies; one
run completed real IPv4 root integration and another completed IPv6 integration.
The ready state requires the actual routing controller's seeded result. These
probes never joined or posted to a room. They exposed and led to repairs for
bootstrap challenge/consensus handling, self-contact hints, optional ping backlog,
Public NODE lookup capacity, retry timing and Public VALUE scope. The final owned
regressions retain the network/identity guards rather than waiving them.

The new exact Public-only runtime admits the shared SDK image and all 47 required
mapped exports. Positive/old-parent/audit/relocation/tamper controls passed;
the persistent Native peer-session test passed in 40.75 s and normal control
retirement in 8.46 s. An initial retirement harness lacked the offscreen Qt
platform; the registration was corrected and that affected test passed. Earlier
source/reference fixture failures remain in local logs; no upstream bug is
claimed from those fixture failures. No fresh sanitizer result is claimed.

The frozen development executable reuses the existing Qt and new SDK installation.
Its launcher/hash checks and generated empty-profile check passed; the temporary
profile was removed. No full build tree or profile backup was copied. The existing
daily-use candidate and its parent SDK remain untouched. The user must exit Java
before checking their real profile; that acceptance, including the separate
resume repair, remains pending. Automated tests have not sent live chat posts.

Limits remain explicit: 128 cached peers and messages, 16 open rooms, bounded
command/IO/network queues; the roster is recent signers rather than an online
census. Peer replacement already evicts an idle least-recent entry when full;
long-running churn/endurance is not qualified. Diversified reads stop a parent
on the first redirect, follow at most four levels/32 keys/32 records within 60 s,
and suppress root membership publication for diversified groups. Full adaptive
publication, persistent contact-cache writes, moderation/bans, I2P/private and
per-torrent rooms remain later work. Disabling history retains the old file and
stops new saves. Whole-plugin/component completion is not declared.

Full original/native pins, executable/provider hashes and local log digests are
recorded in RESULTS.json. Actual room names, keys, nicknames, endpoints, torrent
metadata, personal paths and chat contents are excluded from published evidence.
