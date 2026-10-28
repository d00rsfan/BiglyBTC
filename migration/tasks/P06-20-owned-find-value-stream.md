# P06-20 Owned FIND_VALUE continuation delivery

Status: implemented bounded actor slice; fresh GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P06-20-owned-stream/{matrix.json,review.md}.
Pin original PRUDP/FIND_VALUE behavior to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f before implementation. Extend the
existing owned-loopback Qt6.12 actor with typed FIND_VALUE events. Original
PRUDP retains pending on continuation, delivers every part, removes on final,
and still delivers timeout/error after earlier parts. Do not move away the
callback on the first part, refresh its deadline or retain a packet history.

A shared callback state may be referenced by bounded queued events and pending
operation; QPointer receiver/actor guards preserve cancel/close/death/reentry/
throw/deleting-actor behavior. Reply action/endpoint/connection/transaction bind
before contact parsing; copy scalar transaction/continuation before owner moves.
Check deadlines before/after parse, using existing monotonic fixed expiry.
Pending+queued events keep the existing cap; lack of a partial-event slot ends
that request with explicit Capacity while retaining already queued parts. This
is bounded native probe policy; full source scheduling/privacy/priority parity
and integrated baseline remain pending. Drop callbacks moved to another Qt owner.

Optional inert reply pieces own values/bytes/contacts (never async borrowed
Snapshot stringviews). Bound at16 pieces/256 records/128KiB value data and enforce
1400-byte final datagrams without truncation. Use incoming request skew for value
reply serialization. No real DB, nonce authority, routing/admission, DNS/profile,
ordinary Java managers or public-mode switch. Only new ephemeral numeric-loopback
sockets and explicit owned-peer allowlists; ReferenceFence remains unchanged.

Test multi-part/final and timeout after partial, both reply modes, mixed PING/
FIND_NODE, shared queue/cancel/close/lifetime and packet/type/endpoint bindings,
late replies, Qt owner/receiver movement, callback throw/deletion and bounded
snapshots. Actual source-Java/native FIND_VALUE datagrams must cover both
directions; Java IPv4 only, native IPv6 separately. Qualify fresh GCC16/Clang24/
ASan+UBSan+LSan with all prior wire/readbacks/owned PING/FIND_NODE regressions.
Record full immutable source/native/evidence hashes, preserving original Java
HEAD/index/running client. Then continue real anti-spoof authority, STORE/lookup/
routing/storage/persistence and dependent host/plugin services without stopping.
