# Private conversation connecting windows and lifecycle

Native `4dd98b6de57a338470520bdb50a44b8a78d159d8`, tree `d1acc4272d869a9ef8c2ea980fc3fa10a72f0c99`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; unchanged azmsgsync original
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`. This completes the finite Public
private-window implementation following the qualified authenticated channel,
commands/quit/nickname, NAT and concurrent-view milestones. It does not claim
whole-client or real-router interoperability parity.

Typed pending attempts contain their actual request, signed participant and
parent epoch. Their Connecting windows have no send or reading authority and
no Public join destination. Close cancels that exact request before or during
authentication. The actor checks request/parent scope, cancels the transfer and
drops deferred crypto work. A cancellation racing with success closes only the
actual typed private child epoch; it cannot close a same-text Public room.
The same window adopts the authenticated child and its source view reference.
Failure stays a non-sending error view. Separate room drafts and acknowledgements
remain independent; ordinary per-torrent draft switching retains its old cache.

Original standalone/on-top/sidebar settings remain supported. Original
azbuddy.dchat.ui.last.win.pos x,y,w,h is read and written through the existing
asynchronous profile broker with fresh compare-and-swap, preserving unrelated
fields. Stored bounds describe the outer window frame. Integer extremes and
missing monitors are clamped, valid monitor positions retained, new windows
cascade by16px, and move/resize persistence is debounced1s. Fixed-room controls
remain readable at narrow sizes, with transcript wider than its participant
roster. Escape clears active Find text first; unhandled Escape closes a popout.

Community > Private conversations lists actual active private instances, including
zero-view Keep alive instances. Reopening retains the same conversation and keys;
stale generation/epoch/type refuses. No private key, favourite, history or chat
window is restored as a Public room on restart. Existing unread navigation also
opens the typed private view. Generic open completion waits for a typed source
rather than joining a guessed Public room. Foreground reading remains scoped
by generation and epoch without consuming the previous room during navigation.

Actual private-owner checks exercise two references, Keep alive with zero views,
reacquisition, turning Keep alive off with a live view, and signed quit after the
last reference. During normal application Exit, pending negotiations cancel and
private signed quit is prepared/served within the SAME existing750ms best-effort
pre-stop budget. Hard-stop witness revocation remains immediate. The protocol
is pull-based: peer delivery before global Exit depends on the peer's next
request and is not guaranteed. Ordinary window Close retains the existing bounded
linger and qualifies actual receiver delivery. No artificial push or longer
shutdown timeout was added.

Qualification (generated data, owned loopback SDK sockets, GCC16/C++23/Qt6.12):
- chat-pending-controller: PASS (1.51 s).
- chat-original-window-position: PASS (0.04 s).
- chat-private-pending-cancel: PASS (5.39 s).
- chat-private-normal-exit: PASS (30.99 s).
- chat-pending-window-ui: PASS (1.85 s).
- chat-fixed-page-regressions: PASS (1.83 s).

The profile check includes exact imported/updated geometry keys, stale CAS and
unrelated-field preservation. The real private test includes existing policy,
signed-target, independent-child and no-private-profile-key assertions. Normal
Exit checks local signed status before hard stop and closed source; last-view
release separately checks real receiver quit. Qt tests cover Connecting/cancel,
typed adoption, failure, stale scope, independent views, read focus, geometry,
Find/Escape and reopening retained conversations. Existing page regressions
cover per-torrent draft retention and command/unread navigation. The two images
are generated fixture views, visually inspected; no user screenshot was used.

Still separate: real PublicBound/router acceptance, non-chat NAT adapters and
secondary relay deduplication, friends/moderation/I2P parity, and broader Public
popout session restoration. See the recorded live acceptance task. The footer
rate-gap report is recorded as low priority, without clipping measured values.
The delivered preview, running process/profile, SDK and launchers were not changed.
