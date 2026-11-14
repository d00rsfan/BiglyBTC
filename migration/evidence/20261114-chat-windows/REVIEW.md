# Independent chat windows and epoch-scoped commands

Native `ac6f1e026f2da8d02d5b218a1a0306a031748a64`, tree `27905580c3097e257e9b036e11bab1507a6a09b7`. Pinned original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
BuddyPluginBeta private getClone/destroy; BuddyPluginViewBetaChat
createChatWindow and AC:KeepAlive; SBC_ChatOverview/PopOutManager window options.
Original private clones retain the same actual chat instance. A separate instance
with the same participant pair is a different conversation.

The GUI registry deduplicates a popout for the same actual room epoch, can retain
both a tab and a popout, and releases only the closed view's reference. Closing
the final private view delegates to the qualified signed quit/linger, unless the
original runtime Keep alive reference is held. Private favourite/history controls
are absent. Original independent-window and force-sidebar (native tabs) options
apply; non-independent popouts are parented to the main window and stay on top,
with minimisation available. Accepted incoming private chats open their own view.

The controller watches concurrent immutable transcripts and projects each once
per room epoch/message revision/display policy. Each window has its own draft,
submission acknowledgement and sequence. Generation, epoch, view membership and
typed private role guard actions. The actor revalidates scoped sends/nickname/
participant/display commands when they actually execute; a released or old view
cannot send into a same-text room in a new identity. Queue-pressure retries are
bounded. An opening view cannot send or mark messages read until its source lease
is acknowledged. Reading hints include the room epoch and require actual active,
visible, non-minimised views. Another window cannot clear a different draft.

The Public browser filters private rows and private messages. Generic open
completions defer unknown room roles until a typed snapshot; no placeholder is
joined as a Public room. Private unread navigation opens its typed view. Leaving
the main window disposes its registry while its derived members are still alive.

Qualification used generated data/owned loopback SDK sockets only:
- chat-independent-window-ui: PASS (0.47 s).
- chat-window-scoped-controller: PASS (1.42 s).
- chat-window-owner-epoch-read: PASS (0.33 s).

Qt window tests cover duplicate popout, separate tab reference, Close, independent
versus on-top flags, isolated draft acknowledgements, stale generation/epoch/role,
visible/bound read focus, minimise/hide, and no private transcript in the Public
browser. Actual controller/SDK tests cover independent A/B projections, one-window
acknowledgement, released-view actor refusal, surviving view and rejoin identity.
The existing owner transcript/epoch/rejoin check also passes.

Remaining in this chapter: typed connecting/error placeholders, cancellation of
an exact pending private authentication, original last-window-position config,
Escape/geometry and private signed quit during bounded normal Exit. These are
not claimed complete by this milestone. Real router/NAT acceptance remains a
separate recorded gate. No live profile/process, frozen image, launcher, SDK or
public peer was used or changed. No new daily image was promoted.
