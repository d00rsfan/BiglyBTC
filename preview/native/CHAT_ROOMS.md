# General Public chat preview

Open **Community → Public Chat Rooms** (also available in View). General rooms
are independent of torrent selection and torrent startup. Community also provides
shortcuts to the original BiglyBT General: Help and Beta: Chat rooms. Saved favourite rooms
join automatically; Join room accepts an existing Public room name. Leave room
stops local participation. Enter sends; Shift+Enter inserts a line break. A
successful local submission clears the matching draft; it is not a remote
read receipt. Nickname can be shared across Public rooms or specific to one room.

Original profile identities, favourite flags, nicknames and encrypted saved
history are used without replacing Java files or deleting unknown fields. Close
Java through Exit before opening the same profile in Native. The chat preview
uses the existing configured UDP listener; it does not create another torrent
session or pick another Public port. Original network/proxy/filter restrictions
remain authoritative. A locked, unreadable or changed chat identity is refused.

The Participants list shows distinct signing identities seen in the retained
message window, not a room-wide online census. The bounded window contains 128
messages and 128 cached sync peers; larger rooms are reached through discovery
and bounded peer hints. Managed read-only rooms enforce the original owner
signature. Message markup is displayed literally and cannot load embedded URLs.
Disabling Save history stops new saves and retains the previous history file.

This preview has actual owned DHT membership discovery, signed exchange and
restart/history tests, actual original Java-handler API checks and real Public
IPv4/IPv6 NODE-only connectivity checks. User verification in an existing Public
room remains necessary; automated tests have not posted to a real room. The
separate resume-repair real-profile check also remains pending.

Further parity work includes I2P/private/per-torrent rooms, moderation/bans and
invitations, full participant presentation, persistent remote-contact cache and
long-running churn/endurance qualification. Diversified discovery currently follows up to
four levels, 32 keys and 32 distinct records within one 60-second lookup budget;
it stops a parent lookup on its first redirect, and does not publish a membership
record into a diversified root. Full adaptive diversification/publication is not
claimed. Browser/media features and torrent-facing Azureus DHT are separate work.
