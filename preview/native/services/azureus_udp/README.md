# Owned-loopback Azureus PING transport

Development C++23/Qt6.12 actor over the qualified original PING packet codec.
It binds a fresh ephemeral numeric-loopback port and sends only to explicitly
registered owned peers of the same address family. Public addresses, scope IDs,
port0, DNS/proxy/discovery/router mapping and normal user profiles are excluded.
There is no production/public-route switch or automatic plugin activation.

Every API and destruction belongs to the Qt owner thread. `start` binds the
socket, `allowPeer` records an owned endpoint, `ping` returns an operation ticket,
and `cancel`/`close` finish accepted work. QObject receivers protect callback
lifetime. Move-only callbacks receive owned decoded packets or typed failures.
Destruction drops callbacks. Close/cancel outcomes are delivered through the
bounded completion queue; a completed result already queued before close keeps
its result. Dead targets are abandoned without a callback.

Pending and queued completions share a configured cap (maximum256), as does the
separate peer allowlist. Completion records are removed before callbacks. One
coalesced delivery event processes at most64 callbacks per turn, outside the UDP
socket event stack, and tolerates reentry, close, actor deletion and exceptions.
Datagram draining also processes at most64 per turn and posts at most one
continuation. Oversized input is discarded before allocating/parsing the body.
Native datagrams use the original normal DHT1400-byte budget, in addition to the
lower codec's8192-byte PRUDP bound.

Connection IDs use fresh Qt system random words with their high bit set;
transaction IDs are collision-checked against active operations with a bounded
retry count. Trusted generated-word injection supports tests, including random
failure. Replies must match owned endpoint, network/PING action, transaction and
connection; malformed/foreign/duplicate packets cannot retire another operation.
This binding/queue policy is native probe behavior, not a port of all original
PRUDP scheduler semantics or an authentication claim.

Deadlines use steady time and QChronoTimer with precise timing, rechecking expiry
even if a reply is processed before a delayed timer callback. Socket NoProxy
bypasses the owned test process's global proxy. When configured to serve PINGs,
replies go to the actual allowed UDP sender, echo transaction/connection and
negotiate with the advertised originator version. Empty optional positions,
contacts and no-stats output are deliberate supplied probe state; coordinate
algorithms, routing/store and real statistics are not advertised as operational.

The Qt suite covers15 scenarios/data rows (17 with init/cleanup), including fresh
IPv4/IPv6 peers, version upgrade, mismatches/duplicates/invalid/oversized data,
timeouts/late replies, pending/completion/peer limits, collision/random failure,
receiver/actor lifetime, reentrant/throwing callbacks, proxy bypass, bounded drain
and originator-address route independence. The native/source Java test performs
40 request/reply exchanges (80 actual IPv4 datagrams) across five network/version
cases, with source original constructors/serializers and an unchanged ReferenceFence
restricted to the newly owned native port. The Java socket is also connected to
that peer. Native IPv6 tests and earlier Java IPv6 wire fixtures are separate;
actual Java IPv6 socket interoperability is not claimed here.

The first development run caught an argument-evaluation ownership bug: moving a
decoded packet could reset the referenced transaction before completion lookup.
The scalar is now saved before the move. The failed run is retained in P06-13;
fresh qualification must replay all lifecycle and packet dependency tests on
GCC16, Clang24 and ASan+UBSan+LSan without suppressions.

Source baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Only the original
packet behavior, high-bit demux and relevant transaction/network concepts are
covered. Original priority/retry/send queues, tunnels/proxy/bind/privacy policy,
timing statistics, contact/provider lifecycle, routing/storage/persistence and
dependent plugin/host integration remain pending. This is a local transport
proof, not an operational DHT or complete native client release.

Qt primary API references: [bounded receive/discard](https://doc.qt.io/qt-6.12/qudpsocket.html#receiveDatagram),
[chrono timer](https://doc.qt.io/qt-6/qchronotimer.html). Timers may fire late under
load; no real-time or nanosecond scheduling accuracy is promised.

## Typed FIND_NODE integration

The same bounded actor now exposes `findNode` with an owned20-byte lookup target
and a typed callback, while retaining `ping`. Pending operations, queued results,
random transaction collision checks, deadlines, cancellation and receiver/actor
lifetime handling share one capacity. Supported replies must match the expected
action, connection, transaction and owned endpoint before any contact body can
warm the node cache; normal header network/version checks still apply. Deadlines
are checked both before and after parsing. Completion delivery remains queued,
coalesced and bounded outside the socket event stack. Packet buffers move into
their owning decoder after prefix checks.

An optional owned `FindNodeReply` snapshot supplies random ID, status, estimated
size and at most128 contact snapshots. This has no routing or nonce authorization
meaning. The complete encoded response must fit1400 bytes; oversized snapshots
are rejected without truncation or transmission. Returned addresses remain inert
data regardless of their family or routability. There is no recursive lookup,
automatic admission, DHT listener for public peers, or host/plugin activation.
The node cache is constructed on the actual owner thread at `start`, including
a QObject moved before startup, and released on `close`.

The original PING tests remain; separate mixed/FIND_NODE tests exercise both IP
families, version upgrade, typed reply binding/shared capacity, malformed/foreign/
duplicate responses, cancellation/close/dead receiver, late replies, callback
actor deletion, response budgets and moving the Qt owner. The actual source-Java
UDP helper uses the full qualified FIND_NODE codec in both directions, fixed
public generated IDs/targets and inert17-contact snapshots. Its connected socket
is IPv4 only under unchanged ReferenceFence, with native IPv6 coverage separate.
This remains an owned-loopback transport proof. Routing, anti-spoof token lifetime,
lookup scheduling, persistence and production privacy/scheduling integration
remain required before dependent plugins can run.

## Owned FIND_VALUE continuation delivery

`findValue` adds typed reply events to the same owner/transaction/deadline queue.
Each continuation queues an owned part and retains its pending request/callback;
a final part or terminal failure removes the pending entry. Shared callback state
lives only while pending or queued events reference it. There is no retained
packet-history chain, deadline refresh, fragment sequence invention or reply
reordering. Scalar transaction/continuation reads happen before packet moves.
Part delivery requires a spare slot within the existing pending+queued cap; if
full, that request queues terminal Capacity using its released pending slot,
retaining already queued parts. This explicit bounded probe policy does not
advance the full original scheduler/privacy integration baseline.

Cancellation, close, timeout after earlier parts, throwing/reentrant callbacks,
dead receiver and callback actor deletion retain ordered queued delivery outside
the socket stack. A receiver moved to another Qt owner is not invoked there by
this actor. Reply action/endpoint/connection/transaction bind before body/cache
parsing; deadlines are checked before/after decode and never extended by a part.

Optional supplied `FindValueSlice` pieces own value bytes/contacts. They are
limited to16 pieces,256 records and128KiB payload; individual values stay<=512.
The actor creates temporary borrowed serializer snapshots only during each call.
Replies use incoming request skew and must each fit1400 bytes without truncation.
These are inert test responses with no DB/auth/routing/plugin activation. Only
new numeric-loopback sockets/explicit owned-peer allowlists remain available.

New native tests cover IPv4/IPv6 mixed protocols, three parts/final, partial then
timeout, late final, queue limits, cancel/close/death/throw/deletion, wrong supported
action/endpoint/network, malformed packets, contact mode and receiver movement.
A connected source-Java IPv4 peer exercises three original FIND_VALUE reply parts
per request in both directions, under unchanged ReferenceFence. Full source wire
fixtures qualify arbitrary packet fields separately; actual native IPv6 coverage
is separate from Java IPv4 datagrams. Lookup/routing/storage/nonce authority,
production scheduling/privacy and host/plugin integration remain pending.

## Single-datagram STORE and detailed remote ERROR

`store` accepts an owned `StoreRequest` containing random ID, keys and value
sets. All byte views and nested serializer spans are built only while encoding
the request synchronously; pending work retains no borrowed input. There may be
at most255 keys,255 value sets and256 total value records, with255-byte keys and
512-byte values. Independent key/set counts remain valid wire inputs. The
complete request must fit1400 bytes; larger input fails before installing a
pending operation or sending. This API represents one physical datagram. The
original Java logical STORE batching, priority and first-chunk reporting still
require a separate host implementation.

New `pingDetailed`, `findNodeDetailed`, `findValueDetailed` and STORE callbacks
receive `DetailedOutcome<T>`: a local typed failure or a variant containing the
expected decoded packet or an owned `azureus_error::Decoded`. A remote ERROR is
a received protocol event, not successful admission. Existing PING/FIND_NODE/
FIND_VALUE callback signatures are retained and map such events to appended
`Failure::Remote`. Normal replies and existing lifecycle failures keep their
typed shape.

Every reply still binds transaction, connection and exact owned endpoint before
parsing. Either the pending operation's ordinary expected action or ERROR1032
can match it; an unrelated normal action cannot. Header network/version checks
and both fixed-deadline checks also apply to ERROR. Malformed or foreign errors
leave valid work pending. All decoded error types terminate the pending request,
including after FIND_VALUE parts. Already queued parts precede the terminal
event, using the same shared callback state, capacity accounting and Qt receiver/
actor guards. No packet-history chain, deadline refresh, retry, external-address
mutation or key-block trust is introduced.

Config may supply either an owned `storeReply` diversification string (an empty
engaged string is valid) or an owned `storeErrorReply` snapshot; configuring both
fails startup. These optional responses are inert probe data. A received STORE
does not authorize its random ID, execute admission policy or write a database.
Static FIND_NODE snapshot random IDs therefore retain no authority meaning.
Normal/error response serialization must fit1400 bytes and never sends failed
partial output. ERROR field padding remains the qualified source behavior:
a small incoming packet can own a65535-byte signature, so the256-event queue
cap bounds event count rather than1400 bytes of decoded memory per event.

Source host authorization and STORE admission socket integration, persisted
values, routing/contact side effects, error-driven address/key-block handling,
logical batching and dependent plugin operation remain required work. Native
tests cover both IP families separately from connected source-Java IPv4 peers.

## Generated-only authority and STORE admission service

An explicit optional `generatedStoreService` selects a separate owned FIND_NODE/
STORE service mode. It excludes all inert server reply settings and `servePings`.
Existing client operations and callbacks remain available. The new mode still
binds only a fresh numeric-loopback port and serves only allowlisted owned peers.
It does not open profiles, publish a routing table, activate plugins or store DHT
values in an ordinary database.

Startup validates the entire immutable snapshot before binding: at most255 unique
byte keys,255-byte keys,128 contact snapshots,1..256 receipt slots (default32),
8192 bytes per block field and128KiB aggregate key/block metadata. Supplied key
state records existence, closest ownership, diversification and nullable block
data; absent keys default to nonexisting/nonclosest. These are scripted host
answers. The actor does not infer closest ownership from its socket allowlist.
Contact validation uses the qualified contact serializer; final response budget
remains1400 bytes without truncation.

After binding, startup derives local nodeID from the actual owned endpoint and
configured protocol. The existing P21 Authority and P22 Admission are created on
the actual Qt owner, including an actor moved before startup. A scope rollback
closes the socket and clears service/node-cache state on failed setup. The same
fresh private authority instance issues FIND_NODE cookies and verifies STORE
cookies for its lifetime. It has no profile key, persistence or key-injection
config; close clears key/cache/service state. Provider failures are explicit and
fail closed, preserving the qualified primitive's deliberate departure from the
original catch-and-return-zero fallback.

Complete packet parsing and the ordinary size/action/network/allowlist checks
precede service contact admission. A different advertised numeric IP produces
ERROR/type1 containing the observed transport endpoint, with no authority or
admission work. The comparison ignores advertised port, as source does. Same-IP
different-port requests derive sender nodeID from advertised endpoint and header
originator version. Pending reply binding still compares exact owned address and
port. Source transport-history and control/router-alive observations are kept as
intentions; they do not mutate a host routing/contact database.

`requestStoreToken` sends an actually empty FIND_NODE target and returns a detailed
FIND_NODE result. It does not install a contact/token cache. In service mode,
an empty or other non20-byte target returns no contacts and still issues a real
cookie; a20-byte target uses the supplied contact snapshot. Status, estimated
size, positions and version gates retain qualified packet behavior.

STORE inputs derive sender/local IDs, current steady milliseconds, incoming keys
and value-originator IDs from owned state. They merge immutable metadata by exact
key bytes and invoke the existing admission service once. Verification happens
only at that service's source policy point. Empty/mismatched requests and
unseeded cache forwards therefore skip authority/cache work. Wrong tokens yield
the original DT_NONE1 reply and no Store intention. Correct tokens still obey
forward/range/closest/existing-key/block policy. No policy/crypto algorithm is
duplicated in the actor.

Blocked decisions produce ERROR/type2 at contact protocol>=14, with the exact
owned request/signature. Source pre14 selection remains zero-filled ordinary
STORE diversification; this does not lower the owned wire minimum51. A block
descriptor with null request has `blocked()==false` and null diversification;
ordinary serialization fails and no partial result is sent. A nonnull empty
request is blocked. Provider/policy failure likewise sends nothing and retains
diagnostics. Received block bytes still have no certificate trust meaning.

`takeServiceReceipt` is an owner-only pull of a bounded owned request/decision
record. Each receipt retains the full FIND_NODE or STORE decoded packet so policy
event indices refer to living keys/value sets. It records derived sender and
alive observations, exact successful/failed policy result, auth/identity errors
and send disposition. No user callback runs inside receive, crypto or admission.
A full receipt queue drops service work before identity/authority/admission
effects, increments a capacity statistic and sends nothing; it never overwrites
an old receipt. Packet-body decoding may already have touched its separate node
cache. This explicit queue policy is a bounded development behavior. Pulled
receipts survive close/destruction; queued receipts may still be pulled after
close. `generatedServiceState` exposes owner-only local ID, authority statistics,
admission memo and current receipt count while Bound.

This draft connects real ephemeral token verification and qualified policy to
owned sockets over scripted host state. Actual DB writes/expiry/persistence,
routing convergence, host contact lifecycle, ordinary external-address handling,
logical STORE scheduling and plugin operation remain separate work. No fresh
P25 build, socket interoperability or sanitizer qualification has yet been run.
