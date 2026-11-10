# Original room settings and receive policy

Implementation `39fa39a01b58406a0a7fa4f0a9fca787e2bd6981`, native tree `6dc2d547ac85569c03cf8bb50ef5bc99254b703b`.
Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; original azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.

Per-room sn/nick and legacy Public nickname overrides now follow BuddyPluginBeta
getSharedNick/getNick. Switching nickname mode never copies the old effective
nickname into the global setting. Explicit edits preserve both modern and present
legacy fields so Java migration cannot undo them. Legacy favourite/history maps
are read and updated consistently; the catalogue prioritises favourite/history
rooms over incidental per-torrent options. Unknown fields remain.

Managed suffixes use the original ampersand form [pk=BASE32&ro=1]. Incoming and
loaded read-only-room messages require the manager's actual verified signing key;
a mathematically valid signature from another member is refused. The owner can
post. A newly created managed room inherits matching base-room keys only when
the held profile actually owns that pair. Invalid existing keys are never rotated.
Malformed sensitive policy refuses explicit opening; malformed catalogue rows do
not hide unrelated rooms. Duplicate/malformed managed arguments fail closed rather
than reproducing Java's partial exception recovery. Writable-room moderation/bans
remain separate work; these checks do not claim full moderator-control parity.

SYNC replies retain the original n={u,c} random live-node hint. It remains an
unsigned routing suggestion; decoding it grants no contact/peer/participant rights.
Malformed optional hints do not discard otherwise valid messages. Detailed answers
return the actual requester UID for the upcoming room owner. The actor now permits
six concurrent calls per handler: Java's active_syncs.size()>5 permits six, although
its nominal MAX_CONC_SYNC is5. The seventh call refuses; other rooms remain independent.

All six affected GCC16 suites passed: new managed/hint/nickname/key-inheritance
checks, original profile guards, asynchronous profile, pure message core, the
512-generated-identity bounded-room exercise and DATA with the corrected concurrency
edge. This is source-characterised policy plus Native regression acceptance, not
an actual full original-handler room or live-profile result. No sanitizer/public
network claim. Runtime/UI/discovery integration continues; the user's prepared
build/provider and profile are unchanged.
