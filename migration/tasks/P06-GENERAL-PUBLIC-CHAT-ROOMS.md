# General Public chat rooms

Priority and scope: user decision, 2026-11-09. Basic general Public rooms are
implemented as a development preview, including independently scheduled network
and UI owners. [Qualification and remaining limits](../evidence/20261110-chat-operational-preview/REVIEW.md)
cover actual owned discovery/exchange, original Java handler APIs and real
IPv4/IPv6 NODE connectivity. Real-profile/Public-room user acceptance remains
pending after Java Exit. Whole-plugin parity remains in progress. Preserve the
separate prepared daily-use candidate and its parent SDK.

This chapter implements shared, general-purpose Public chat rooms with
many participants. Per-torrent rooms and private one-to-one sessions are later
work. The target is interoperability with original BiglyBT/azmsgsync rooms,
not a separate Native-only messaging network.

## Minimum dependency path

1. Connect only the Azureus facilities used by azmsgsync: compatible network
   identity, bootstrap/contact discovery, room-key get/put and the original
   registered transfer-handler request/reply transport. Complete cancellation,
   fragmentation/reassembly, timeouts and shutdown where that transport needs
   them. Mainline DHT is a separate network and cannot supply this contract.
   MsgSyncHandler consumes DHTPluginInterface/transfer handlers directly; porting
   every generic DDB API or torrent peer-announcement feature is not a prerequisite.
2. Integrate the existing crypto/envelope primitives with the actual general-room
   synchronization protocol: original maps/compression, message signatures and
   sender identity, ordering, duplicate/replay handling, peer selection and retry
   scheduling. Private-session STS integration is not a prerequisite for ordinary
   group-room delivery; its existing tests and primitives remain available.
3. Preserve the shared profile's chat identity, nickname, favourite/autojoin rooms,
   room options and saved history using the original formats and unknown fields.
   Never overwrite a locked/unreadable identity or silently substitute another
   identity. Keep profile writes under the application's existing ownership.
4. Add an independent Chats entry/tab and room list with join/leave, receive/send,
   participants, unread indicators and history. Joining a general room must not
   require selecting or starting a torrent. Retain room selection and relevant
   saved options across Java/Native handoffs.

The functional dependency is Azureus-for-chat -> general-room sync -> usable
rooms. Profile/history and UI work can be developed alongside protocol work once
interfaces are fixed. Full Azureus torrent discovery/announce parity follows
working rooms; swarm discovery follows that. I2P chat, per-torrent rooms and
one-to-one chat are outside the first general-Public-room acceptance scope.
Swarm merging remains immediately before the final browser/media-player work.

## Many-participant and usability acceptance

- Exercise actual Java/Native room-message exchange with owned test participants,
  including history catch-up, reconnect, duplicate/out-of-order delivery, packet
  loss and participant churn beyond the local peer-cache size.
- Preserve original bounded peer/message windows and scheduling semantics.
  At the pinned azmsgsync source the handler uses 128 cached nodes, 128 messages
  and a nominal concurrent-sync bound of5; these are local protocol bounds, not
  a total-room-membership limit. Do not build a full mesh or expand unbounded
  queues to support a busy room. Derive edge behaviour from the original code.
- Use lightweight generated participants/replay for larger load tiers, with a
  smaller genuine Java/Native interoperability set. Avoid a JVM per participant.
  Check CPU, retained memory, timer/backoff behaviour, pagination/batched paints
  and a responsive composer while messages and participant changes arrive.
- Chat network processing must not wait behind the complete torrent-startup
  queue. Keep UI painting and transport scheduling independent of blocking
  library work; serialize compatible profile/history writes through their owner.
- No actual room names, messages, participant keys, nicknames or profile data in
  public commits, screenshots or fixtures. Do not post test messages to real
  rooms; live posting requires explicit user action or authorisation.

## Open runtime regression

The user reports that the current Native progressively leaves Waiting over
several minutes while one CPU core remains fully occupied and disk activity is
low. This is an unresolved startup/admission performance regression; exact cause
and Java-relative measurements have not been established on the current build.
It is not evidence of a permanently stuck queue. Do not mark it solved, silently
attribute it to hashing, or interrupt the running client. General chat rooms now
have priority; retain a separate CPU investigation after that priority decision.

## Delivery discipline

Batch implementation and run affected checks at useful boundaries. Read concise
build/test summaries and relevant failures, retaining full logs locally. Reuse
existing caches; no duplicate build/Qt trees or accumulated temporary artifacts.
No repeated unchanged-source, local-commit or remote-commit validation. Record
provenance at delivered milestones and review public changes for private data.

## 2026-11-10 pause checkpoint

The SDK shared-port bridge and complete reproducible overlay are saved. Actual
IPv4/IPv6 uTP payload, UDP tracker and BEP5 coexistence, exact socket selection,
source/close/proxy rejection and cancelled-send behaviour passed. This is a
lower-layer milestone, not a functioning-chat release. No whole-component
integration baseline or default runtime is advanced.

Resume with a dedicated room worker and bounded SDK ingress mailbox, the shared
physical-channel adapter, relevant network/key policy, Public room discovery,
scheduler and Qt page wiring. Finish nickname/options and managed-room receive
policy. Preserve the existing prepared user launch runtime. Do not repeat green
coexistence tests without a relevant change. No next milestone is started during
this user-requested pause.

## 2026-11-10 resumed: independent room owner

User resumed the chat chapter. The room owner, actual owned DHT membership/exchange/history and menu/Host integration are implemented; see [evidence](../evidence/20261110-chat-room-owner/REVIEW.md). Continue with the exact new Public app cohort, actual original room-handler interoperability, and UI/runtime review. Earlier pause notes are historical. The chapter remains active and incomplete.

## 2026-11-10 Public preview delivered

Earlier pause/incomplete checkpoints are historical. Native implementation `a1096b5d1fab9988d137cb05d351d0fca6b4c369` supplies the Public-only runtime, original conversation ordering/history continuation, DHT bootstrap and bounded diversified discovery, fenced full-handler checks and classic UI controls. See the [operational preview evidence](../evidence/20261110-chat-operational-preview/REVIEW.md) and [usage](../../preview/native/CHAT_ROOMS.md). Existing-profile/room acceptance is deferred until Java Exit; no live chat posts or personal-profile writes were made during this work. Further parity and endurance limits are recorded explicitly; no unrelated chapter is started.

## 2026-11-10 live preview feedback

The first full-profile chat preview exposed a publication-order bug: its service
was queued to the GUI before ready() published the profile epoch, so the stale
session guard discarded it and the room list/status stayed empty. Fix publication
after the profile identity and cover full SessionBridge open/close with saved
rooms; standalone page tests cannot establish this integration. Standard room
shortcuts must retain the original BiglyBT Help/Beta keys, not renamed Native
rooms. User screenshots and saved room/identity details remain local.

Requested follow-up: a lower status-bar unread-chat indicator that opens the
room with unread messages, including general/managed rooms and, after their
implementation, per-torrent rooms. The current roster is recent signers only.
This status-bar work remains open; no tray/sound/notification parity is claimed.

## 2026-11-10 original chat identity repair

User confirmed the saved favourite list loads. Original Java public keys use 49-byte uncompressed SEC1 points; the Native fingerprint and managed-owner guards wrongly required 25-byte compressed points. Implementation b67c79cb1eaae7ab6c3f23142fb97549c8a28b0c preserves exact keys/UIDs and passes an original-Java-generated identity regression that failed before the fix. Four focused suites pass. See [evidence](../evidence/20261110-original-chat-identity/REVIEW.md). Restart/real-room acceptance remains pending. User requested one-problem work: Waiting/no active checks and sustained CPU remain equally critical and unresolved.
