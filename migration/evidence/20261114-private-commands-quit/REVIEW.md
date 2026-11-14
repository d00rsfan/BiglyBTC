# Private commands and signed quit/delivery milestone

Qualified native `cfc473fbb29dbd02f4af278faf675d8b9f130b85`, tree `81edf5ea44edb1c5718885e42a534ebcb80817a6`, using identity plugin
`2bc28c26c669734b2343215fbb2291c54e96e972`. Originals: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` and azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.
This follows [private channel/owner/menu qualification](../20261114-private-chat-channels/REVIEW.md).
Whole private conversation/UI parity remains open. No running user profile,
image, manifest, launcher or SDK was modified; no daily-use image was delivered.

## Commands

`/msg` and `/query` are decoded before routing and produce a typed private
request. The exact current signed participant key and parent epoch are required;
missing, own and ambiguous nicknames fail locally. Original ASCII separators,
case-insensitive command names, NBSP nickname tokens and initial-message trim are
preserved. Ambiguous names are deliberately refused rather than selecting an
arbitrary matching identity. Unavailable/nested private commands cannot become
public text. Unknown-command and decoded/long-private-command isolation remains
covered by the existing owner/parser checks.

The initial private text waits for a completed authenticated child and uses its
ordinary bounded composer/send pipeline. The original composer request is
acknowledged only after initial parts are accepted; failures keep the original
source-room/text association. The opened child is returned through the existing
typed UI route. Handshake failure sends no initial payload to a public ledger.

## Quit and bounded delivery lifetime

The original empty-message payload with `f.s=1`, nickname, signature and ordinary
previous-ID/sequence is generated only for an actual private Room. Received
status uses Java int narrowing; quit projects as an information line `%1 has
quit`, without repeating the nickname prefix or inventing user payload text.
It does not generate ordinary empty-text unread attention.

Local private messages track the original seen/probably-seen evidence. A matching
peer request bloom increments probable sightings; matching message counts with
no tombstone evidence confirm seen. A verified recirculated own message also
confirms receipt. Pending means no seen evidence and fewer than five probable
sightings. Tracking is bounded by the retained message window. Sending a reply
alone does not imply the peer has seen it.

Closing a private conversation prepares/signs outside source authority, commits
under fresh authority, hides its row and retains only the bounded private
session for delivery. Normal sends check closing both before signing and at the
final commit, so queued prepared text cannot follow the quit. Linger checks at
5s intervals and ends on all-seen or at60s; source revocation ends it immediately.
The unrelated background-room collector cannot discard a private linger early.
Application shutdown force-closes these sessions immediately and does not wait
for that60s per-conversation limit. No private history/identity is persisted.

The source rules come from BuddyPluginBeta's /msg,/query, private destroy,
getMessageStatus/getMessageType and azmsgsync's handleRead,
getUndeliveredMessageCount and destroy. New delivery/timer checks are native
boundary tests against these inspected pinned rules, not an additional exact
Java plugin timer oracle or a full SWT lifecycle test.

## Validation

- Affected service/UI/desktop-host, owner, shared-SDK and pure test targets compile
  under GCC16/C++23/Qt6.12.0 Release. Compiler output stayed in local logs.
- Four related suites pass in6.75s: existing exact Java private handshake/policy,
  public rooms, original composer plus private parsing, and participant/quit
  projection. Private checks include prepared quit without ledger mutation,
  original five-probable/seen thresholds, signed flags/sequence, public refusal,
  and synthetic5s/60s boundaries.
- Actual shared-SDK private exchange now carries quit and seen evidence over
  IPv4 and IPv6:4 passing QTest entries,0.94s wall. No public payload/peek/history
  fallback is enabled.
- Actual generated GeneralChats owners pass in30.49s: original fresh policy and
  key/epoch checks, private-only /query initial payload/ack, both quit flows,
  independent child traffic after parent close, no child crypto identity and
  public join/save rejection.
- 23 passing QTest entries at2x in1.93s include exact single-prefix information
  rendering and existing participant menu/transcript/preferences checks. The
  preceding menu screenshot remains in the channel review; no new system-screen
  capture or current-profile/UI acceptance is claimed.

The first compile caught a template comma in a new Qt assertion macro; it was
fixed before checks ran. The final build also includes the closing guard at the
ordinary send's final source-gated commit. Unrelated long green network suites
were not repeated. Next: private nickname controls/original config handling and
remaining connecting-window/refcount/keep-alive behaviour in this chapter.
