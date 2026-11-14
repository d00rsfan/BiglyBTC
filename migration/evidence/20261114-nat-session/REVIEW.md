# Source-bound three-party NAT actor

Native `056f1ccaa4367dd05fca50b61c2ad6193fdee787`, tree `4646732ab846353640a6d4e4f902b79e855bb58f`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; original MsgSync caller baseline
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6` remains unchanged.

Session combines the previously qualified original codecs, binding leases,
client heartbeat and tunnel clock policies on one existing shared UDP lane. It
registers the original NAT handler with explicit Call/empty-key Write and same-IP
external/physical roles. It opens no socket and grants no peer/routing authority.
Explicit relay selection and already-admitted physical peers are required.
No application registration, DHT lookup/publication or automatic tunnelling is
activated by this source step.

Incoming parsing is scheduled after source release; every reply, relay, bind,
raw tunnel write and timed turn reacquires the owner permit. The epoch is fixed
at construction from an already-held source revision. A changed revision or
refused permit closes the actor, cancels transfers and cannot revive an old
request. Receipts, source revision, handler, actual peer, deadline and external
identity remain separate. Independent handlers and admitted routing peers survive
NAT actor shutdown. Completion data alone grants no authority to a consumer.

A caller is entered before its relay request, so an early inbound write can be
remembered before the relay reply. The relay uses a current binding lease and
retains the target's original physical route. Connect is accepted only from the
selected relay's external address. Its first inbound write completes or times
out before the Connect reply, then bounded retries can continue. On the caller,
the ten-second wait begins after its first outbound write's ACK/timeout, matching
the original blocking write followed by wait. A direct observation wins over the
relay's suggested port, while full external identity/export is retained. A raw
transport ACK does not establish a direct observation.

The chat-only adapter accepts absent/empty client maps. Nonempty generic message
payloads, _notunnel and debug execution are excluded; this slice never relays
private chat payload or keys. Original Query reports the actual physical source
port and Java-style expanded IPv6 address. Close invalidates a matching selected
relay. State is bounded to eight server bindings and three pending calls/relay/
connect groups of each kind; existing DATA per-handler/global limits still apply.
Heartbeat failures and timers reuse the already qualified original policy.

## Validation and limits

Generated owned SDK IPv4/IPv6 three-party exchange covers ordinary endpoints
and an explicitly labelled header seam with declared port different from actual
socket port. It verifies early inbound ordering, direct route selection and
unchanged external address/node identity, plus Query address/physical port.
The real SDK send/receive/current witness remains in use; Provider::channel is
unchanged. This is not a real router NAT mapping or observed public-address test.
Wrong relay, opaque _notunnel payload and unsolicited inbound are refused.
Cancellation, the three-request bound, changed source, closed reuse and an
independent handler after shutdown are checked. Test hooks assert that revision
is read only under a permit and decoding follows release, and reject nested
permit acquisition. The existing shared fixture is extracted into one header;
its only behavioural instrumentation tracks source preflight scope.

Exact original wire/contact/binding/client reference evidence remains linked in
the prerequisite reviews, not rerun or represented as combined original-Java
NAT interoperability. New combined actor qualification uses native actors on the
pinned actual SDK. Full original implementation interoperability and real NAT
acceptance remain open. First-unknown ingress, handler-scoped route admission,
logical/physical updates for pending chat transfers, automatic relay selection,
lookup/publication, expiry Close notices and secondary relay de-duplication still
require integration. The user's frozen preview, SDK, profile and launcher are
not changed by this NAT milestone. No separate reviewer or subagent was used.

The focused new SDK suite passed six test entries in2.28s (plus setup/cleanup).
Four retained SDK roundtrip/address-role cases passed in1.30s. The final grouped
build passed in41.45s. Two strict indentation warnings in the new conversion
helpers required correction; the first correction missed the second line and
that incremental build also failed. No failed build is counted as qualification.
All commands/results are summarised in [qualification](qualification.json).
