# Bounded explicit I2CP datagram receiver

Source only. Apply the seven `staging/` leaves after checking the four exact
beforeimages; append the two marked sections of `CMake.fragment` to the existing
services/tests CMake files. The four production inputs still match implementation
`1be0a4df444e82d3c2e702782bae88f5536959ef` and current accepted source
`d384a850980f1b6245230ea852d617b11c405e11`. No existing test file changes.
In particular, preserve the accepted `1481` bounds correction in
`tests/i2cp_datagram_tests.cpp` (SHA in the manifest's composition inputs).

The existing `services.i2cp_initial_session` / `F06.i2cp_initial_session`
component remains in progress. There is no router, Host, public-network, default
receiver, Streaming, OPEN, tunnel health or end-to-end delivery activation here.

## Original contract

`source-pins.json` binds fifteen actual retained Git blobs at official I2P
`9134f808337b401e8e53c73734c81fab04280c9d`, tree
`b38c70fbf3f7ce6f33ca95bedaa001e1458a4c9b`. Thirteen core files exactly equal
the pinned 2.13.0 source JAR; the two router files equal retained tree blobs.
Four missing blobs were materialized only after source-JAR bytes matched their
exact tree object IDs. The modified `azi2pcore` JAR is a separate finite
interoperability reference, with no whole-source/core equivalence claim.

`MessageStatusMessageHandler` sends ReceiveBegin6 on Available status0.
`MessagePayloadMessageHandler` stores the unencrypted Payload31 bytes and sends
ReceiveEnd7 in legacy mode before application decompression/verification or
consumption. `I2PSessionImpl2.receiveMessage()` drops failed gzip without
destroying the session. `ClientMessageEventListener.handleReceiveEnd()` directly
removes the payload by ID; it does not require a preceding Begin. Therefore a
full/disabled native legacy receiver may release storage with End without
fetching the body. That is an explicit bounded-drop choice, not app acceptance.

The router captures `i2cp.fastReceive` only at initial creation using Java
`Boolean.parseBoolean`; reconfigure cannot alter it. The native predicate
preserves exact key spelling, case-insensitive `true`, no trimming, and raw
absent=false. The Java client's absent=true producer default is not silently
injected. Current owned LS2 already requires a router newer than the original
fast-receive version gate. Fast Payload31 receives no Begin/End. Unexpected
Available frames in fast mode are dropped without legacy controls.

## Receiver and lifetime

`enableDatagramReceiver()` is explicit and requires an armed owned lease-session
signer. Application admission additionally requires the actual current session,
same complete accepted Destination, retained live signer, connected generation
and unexpired LS2 signing/enqueue observation. This observation is not a router
acknowledgement. Signed Datagram2 verifies possession of the sender signing key
and binding to the current recipient hash; it does not authenticate every raw
sender Destination field or freshness. Datagram3 is expressly an unsigned
claimed hash. Both use the already qualified bounded mux/gzip/datagram codecs.

Legacy fetches have one current session/message/generation and one independent
absolute deadline, captured on Available. Begin must actually be written before
a matching Payload can enter the application queue. A late or wrong message
cannot complete another fetch. Timeout/disable release only the relevant router
storage and never manufacture remote delivery/status facts. Reconfigure/lease
renewal do not extend the fetch deadline. After complete matching receipt, End
is queued before application checks; malformed gzip, wrong recipient/signature,
unsupported protocols and queue overflow drop locally, leaving valid outbound
requests usable. Outer framing, transport and resource failures retain their
existing strict terminal behavior.

There are at most four unread datagrams and 160 KiB owned decoded sender wire/key/hash
plus payload bytes (small container metadata is additional), one legacy fetch,
four queued 11-byte controls and one control socket write at a time. No ACK is
queued behind a blocked socket without this bound. A full control queue records
`controlLost`; storage release can then be incomplete until router cleanup or
session close. This is explicit backpressure, not an unbounded ACK promise.
Transient processing retains the bounded outer frame, at most one 64 KiB payload
copy and bounded 40 KiB inflated datagram/body copies; 160 KiB is the unread
queue budget, not a claim about total process memory.

Unread IDs remain duplicate-protected even when the fixed 64 recent-ID ring
rotates. Popped/dropped recent IDs have a finite window, not lifetime replay
protection. Disable clears unread data and cancels a legacy fetch; terminal or
close clears every receiver/control object. Receiver notifications are coalesced,
generation-tagged, queued and QPointer guarded; callbacks may pop, disable,
close, restart or delete. No code accesses the owner after a public receiver
signal. Setup allocation failure maps to the existing resource fence.

## Root qualification

New targets/CTest names are:

- `biglybtc-i2cp-receive-control-tests` / `pure-i2cp-receive-control` (60 s).
- `biglybtc-i2cp-receive-channel-tests` / `i2cp-bounded-datagram-receiver` (60 s).

The pure test covers exact unsigned Begin/End fields/bounds and raw initial
boolean spelling. Owned TCP tests cover actual legacy/fast signed2 and claimed3
over IPv4/IPv6, simultaneous outbound status progress, malformed/recipient/
signature/protocol drops, four-message/160 KiB queues, End-without-Begin release,
unread-ID protection beyond the recent ring, real fetch timeout, late/cancelled
payloads, immutable initial mode and callbacks which consume/disable/delete.
They do not claim an actual OS-level stalled socket control-overflow experiment.
Run the affected existing channel, LS2, outbound datagram, codec, identity and
session tests because shared channel dispatch changes; keep all old fixtures.

`OFFLINE-COMMANDS.sh` takes fresh absolute output, the newly built pure control
test ELF, existing synthetic fixture directory, pinned official JAR and pinned
modified JAR. One javac builds `ReceiveVectors`; each JAR separately produces
four actual Available/Payload/Begin/End plus Datagram2/3 cases and verifies native
exports. The native side verifies Java-signed packets and exact Begin/End bytes;
the Java side verifies native signed packets and message fields. DSA signatures
are verified, not compared for random-byte equality. Root should bind actual
current source/ELF hashes, a 180 s outer budget, all four row labels in each
direction/reference, and exact class-load `file:/` origins. Existing synthetic
keys stay separate from profiles; no session, router or endpoint is constructed
by the reference bridge. No compiler, Java/native subject or endpoint was run
while authoring this capsule.
