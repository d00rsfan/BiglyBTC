# Original empty-key DATA write prerequisite

Native `936a0f2bab2d67f3c4c13662e0acce27c2e7c8df`; tree `494a31bf2dce590bfd5e503135ceb1ab5cdc7c1e`. Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The original NAT puncher uses ordinary calls and one-way empty-request-key DATA
writes on the same SHA-1 handler key. The native transport now admits the latter
only with an explicit opt-in or Write registration. A write completes on its
transport ACK and cannot be answered as an application call. Default Call and
Read registrations retain their roles. Values remain bounded to16KiB, operations
to30s, with existing endpoint/network/source/receipt/cancellation limits.

Fragments, missing-range replies, exact duplicate ACKs and receipt expiry use
the existing transport path. Changed-payload replay is refused. Done incoming
receipts and reply caches now arrange their expiry wakeup, without traffic-driven
retention. A queued receipt is tagged with its source revision: the consuming
owner validates it in its own source gate after the transport permit is released.
No new peer admission, profile read/write or listening socket is introduced.

The affected native regression suite passed in4.22s. Actual original Java
DHTTransferHandler exchanged40,4096and16384byte writes in both directions:
8QTest entries including setup/teardown passed in6.25s. The original DATA body
constructor/serializer statements are extracted from the pinned source; the
unchanged transfer handler is compiled from source. Generated Adapter/header/
contact seams use only declared loopback peers. SHA-256 scope is recorded in
[source-scope.json](source-scope.json).

Actual shared SDK IPv4/IPv6 writes of0,40,4096and16384bytes, ordinary calls on the
same handler, encrypted public/private exchanges and DDB reads passed together:
12QTest entries in3.31s. New source isolation/replay/cancellation cases and existing
call/read regressions ran together. [Qualification](qualification.json).

During qualification, an added post-release source-permit check broke normal
owner delivery; it was removed and the related suite passed. The original peer
fixture also needed to observe asynchronous handler completion on socket timeout;
that fixture correction was followed by the final six exchanges. Neither issue
changed the running client, its frozen executable or the real profile.

This verifies the bounded transport prerequisite. NAT publication, rendezvous
selection/bind/expiry/punch/connect owners and actual NAT assistance remain in
progress. No real NAT or public-router result is claimed. The private chat
chapter remains open, including remaining window lifetime/preferences.
