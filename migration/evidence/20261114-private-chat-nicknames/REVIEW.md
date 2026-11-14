# Private nickname/config milestone

Qualified native `45c7f841c7128698e5bd3b257484f6589eed8883`, tree `88e9031bdc98431b791dc252861d0dbf4b275c2a` against BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
This follows [commands/quit](../20261114-private-commands-quit/REVIEW.md).
Whole private conversation parity remains incomplete. The user profile, frozen
image/manifest/launcher and SDK were unchanged; no daily image was delivered.

Private Shared nickname/Nickname controls now use the original
`azbuddy.dchat.optsmap` Public+base32 room option key, `sn`/`nick`, legacy nickname
fields where present, and `azbuddy.chat.shared_nick`. The private conversation
has no separate saved crypto identity. Both mode and nickname writes bind the
actual parent identity stamp and live private epoch through the IO broker;
missing/changed parent sources or inactive requests are refused. Successful
private writes create only original nickname option metadata, never favourite,
history or child crypto records. Incidental nickname options do not reopen a
closed private conversation as a public room.

A catalogue refresh applies only nickname/mode to an existing typed private
room, preserving private role, inherited default identity, non-favourite,
non-saving and private transport policy. The original global shared name is
returned separately and updates all currently shared live rooms, including
Public rooms without their own saved option entry. Participant preferences are
not replaced. A private nickname can still change after the public parent view
closes because its source proof is the saved parent identity, not an active
parent window. Public saved options remain disabled for private conversations.

Relevant original symbols: BuddyPluginBeta getSharedNick/setSharedNick/setNick,
ChatInstance setSharedNickname/setInstanceNickname/getNickname and
ChatParticipant.createPrivateChat; existing original config encoding retained.
The current generated private labels/inheritance were already qualified; full
legacy private label/default import and window lifecycle parity remain open.

Validation: affected profile/owner/UI/desktop-host targets compile. Three related
profile suites pass in1.49s, checking inactive/incorrect/correct parent proof,
original fields, shared mode/value and no child crypto identity/unrelated-field
changes. The actual owned-profile private scenario passes in30.45s: shared-name
propagation into a Public room with no own options, preserved private role,
per-conversation nickname visible in its signed payload, and further nickname
edit after parent closure, alongside previous private command/quit checks.
At2x,23 QTest entries pass in2.01s with private nickname controls enabled and
favourite/history still disabled. Only the final test-only owner assertion was
rebuilt/rerun; unchanged green profile/UI checks were retained. No current-profile,
SWT, sanitizer or real NAT interoperability acceptance is claimed.

Next dependency audit: original MsgSyncHandler.getSharedSecret calls tryTunnel.
The qualified private route currently proves direct owned endpoints. Native
rendezvous/tunnel integration must be audited/ported before claiming coverage
for peers which require NAT assistance; then finish connecting/popout/sidebar,
clone/keep-alive/read-focus semantics. Private view restoration must never
reinterpret private state as a public join/lookup/registration.
