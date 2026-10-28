# P06-24 Owned STORE transactions and detailed ERROR delivery

Status: implemented bounded transport; GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P06-24-owned-store/{matrix.json,review.md}.
Original source pinned before implementation to
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f original UDP sendStore/process/reply
processing, STORE/ERROR codecs and PRUDP continuation/terminal behavior. Source
contact/host audit is retained with full file/method hashes. Add bounded typed
single-datagram STORE operations and detailed typed ERROR events to the existing
owned-loopback Qt6.12 actor, preserving all existing public callback signatures.

DetailedOutcome<T>=expected<variant<T,ERROR::Decoded>,Failure> for new detailed
PING/FIND_NODE/FIND_VALUE callbacks; existing methods adapt remote errors to a
new appended Failure::Remote. STORE uses detailed normal/error replies. Bound
ERROR1032 is permitted for any supported pending action, only after original
native endpoint/transaction/connection binding and network/version/deadline
checks. Malformed/unbound errors never steal a legitimate pending request.
ERROR is final even after FIND_VALUE continuation parts, preserving earlier
queued callbacks, fixed expiry, bounded queue and QObject lifetime/thread guards.
Read scalar identifiers before moving decoded ownership.

StoreRequest owns randomID/keys/vector-of-owned-values. Encode borrowed snapshots
only synchronously during submit, never retain string_views after owner lifetime.
Keep exact1400 send budget and bounded input counts. This milestone sends one
physical datagram; original logical STORE chunking/first-packet completion,
priority/retry/contact promotion and ordinary host semantics stay deferred.
Optional owned STORE normal/error server snapshots are inert test replies only;
no token authorization/Admission/database writes are enabled by them. Actual
P21 authority/P22 admission socket integration follows separately. Do not use
static FIND_NODE snapshot cookies to authorize STORE.

Native Qt tests cover IPv4/IPv6, normal/error/unknown types, old API wrappers,
mixed operations, partial FIND_VALUE then terminal ERROR, malformed/foreign/late
errors, caps/cancel/close/death/thread/reentry and owned snapshot lifetimes.
Actual unchanged source Java STORE/ERROR datagrams run both directions on only
new connected owned IPv4 loopback ports; native IPv6 separately. Fresh GCC16/
Clang24/ASan+UBSan+LSan with all prior source packet/readbacks/owned UDP regressions.
No DNS/public router/profile/real identity/ordinary core/plugin activation. Keep
original Java HEAD/index/running client untouched. Save immutable evidence and
provenance, continue actual authorization/admission/routing/storage/host work.
