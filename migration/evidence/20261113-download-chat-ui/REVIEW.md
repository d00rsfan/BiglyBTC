# Canonical Public chat in torrent properties

Native f48065ebb5fc0064f4918598720ca37aa6bf3059, tree
3561e788e8d8bac1fe63495fa7dddfa2bdab716a. Original BiglyBT baseline remains
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; azmsgsync remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. Canonical key/subject and transient
preview qualification are in ../20261113-download-chat-peek/REVIEW.md. Original
BuddyPluginView.addBetaSubviews/source/focus lifecycle and ChatView embedded
view behavior inform the properties integration. This is native UI/owner
qualification, not a complete live SWT GUI or every chat preference claim.

A Chat property tab opens the actual owned Public canonical room, independently
of rating score/plugin and the general-room selection. Saved identities,
nickname/shared-nickname mode and saved history are reused. Only explicit open
creates a missing identity; the separate peek remains identity-free. Private,
metadata-only, absent or non-Public download subjects are refused. Source and
identity are checked before identity creation and on every physical room turn,
including local DHT membership publication. The immutable room token is replaced
when a new download association is required; an existing identical association
retains its real session/peers and original discovery clocks on reopening.

The GUI watches one (profile session, download hash), uses a fixed-room chat
widget and rejects foreign room/old download packets. Drafts remain per room.
General room selection/transcript stay independent. Read visibility includes
only the focused, visible property Chat. Actual per-room messages are immutable
shared packets cached by the room mutation, avoiding a full copy of every
background transcript on every owner pulse. Opening/rebinding preserves the
source-bound key; it does not substitute a generated identity for unreadable
original keys. Source-bound ordinary sending now signs after source release,
then commits under a fresh permit; failure retains the user's draft.

Real owned IPv4/IPv6 tests fetch existing history, deliver a signed response to
another client, keep an independent general room selected, retain the same
transcript packet/session on reopening, and refuse a send after Public is
withdrawn without adding the denied text to either ledger. The final scoped
owned-download-chat-peek test passes in25.60seconds with zero skips. Earlier
full general-room service regressions pass in29.47seconds after the send change;
this includes existing history/nickname/unread behavior and the diversified
transient peek. Native fixed-room/stale-packet/draft/property routing tests and
the full general chat UI regression pass (full UI1.13seconds). A focused200%
Qt run passes four results, zero skips in0.42seconds. The generated screenshot
[properties-chat-2x.png](properties-chat-2x.png) was visually inspected; it
contains synthetic content only. It is not the user's running client.

The production desktop host static library and main_desktop.cpp object compile.
No executable/image was linked or installed for daily use; the frozen preview,
SDK artifacts and real profile were untouched. Automatic read-side peek-to-
autoNotify and global status-bar notifications remain the next milestone of
the same chapter. General preference/moderation/history lifecycle parity and
non-Public providers are not implied by this bounded Public integration.
