# Original Public NAT wire prerequisites

This library contains codecs, without sockets, profile access or a NAT owner.
The original handler is SHA-1 of Aelitis:NATPuncher:TransferHandlerKey. Requests
and replies use the original type0..11 bencoded maps. Contacts reuse the existing
original UDP contact codec; publication prepends version0. Publication lookup
input is node ID followed by :DHTNATPuncher, before the normal DHT key hashing.

Socket-address binding keys use Java numeric InetSocketAddress spelling,
including uncompressed IPv6 in brackets. IPv4-mapped IPv6 publication/address
input is canonicalised as Java InetAddress.getByAddress does. Client data stays
a bounded opaque dictionary. The codec never publishes it, treats it as an
application instruction or executes original _debug reflection; root _debug is
refused. Duplicate dictionary keys, malformed types, invalid contact envelopes
and oversized/deep messages fail locally. Active endpoints require a nonzero
port; unsupported publication versions/trailing bytes are refused.

The pure original-method oracle extracts contact/key/publication methods,
request-map construction blocks and five exact serializer methods. Its memory
interfaces deliberately omit full Java UDP utility/interface initialisers,
which would start global configuration/security facilities. Original BEncoder
still produces the compared map bytes. This qualifies formats and generated
inputs; it does not qualify original asynchronous NAT/router/owner behaviour.

Next: bounded binding/client/punch state and receipt/source policy, explicit
NAT-only ingress for an expected peer's observed port, owned three-party/shared
UDP exchanges, then actual owner/publication/chat integration. Direct endpoint
tests do not establish NAT traversal or full private-conversation parity.

The separate Bindings memory policy now models the original server's eight-entry
cap, secondary refusal, rebind without replacing the saved contact, strict
five-minute expiry and eight-failure/strict30-second retry window. A successful
connect resets failures. Explicit maintenance matches the original timer's role;
bind/candidate do not perform eager expiry. Serial leases prevent an old async
completion from changing a replacement entry. The future owner still validates
source/endpoint/deadline and consumes operation completions once. Counter wrap
is saturated/refused. Client selection/keepalive, secondary de-duplication,
publication and actual relay/punch effects remain outside this policy.

ClientBinding now models two initial failures, four failures after an operational
bind, immediate explicit refusal,30s attempts and50s start intervals. Stale or
late success cannot restore a cleared client. Punch/TunnelBurst model four send
opportunities,3s per send,10s initiator wait, indirect-port fallback and actual
inbound same-IP port priority. An outbound ACK alone does not confirm an inbound
tunnel. Delayed ticks never catch up with a burst; deadlines and counters remain
bounded. Exact original bind-result counters are compared with memory callbacks;
retry/wait clocks use native deterministic tests and inspected original source.
These policies do not yet own sockets, selection, publication or chat routing.

Address-role audit: original contacts carry an external address/identity and a
separate actual transport endpoint. receiveBind reports the external port;
sendConnect and the relay punch reply use the stored transport port. The existing
binding model stores the external contact. Actual-owner integration must retain
both roles, including the original rule that rebind keeps the initial contact.
A NAT port update never grants a private participant identity; STS must still
validate the selected full signed peer key.

RoutedContact now retains those two roles. It derives the ID from the declared
external address, canonicalises mapped addresses and allows a different physical
port only on the same IP. Export/transport updates preserve external identity.
Bindings accepts this route and keeps both initial roles across rebind. Geometry
validation is not source/peer authority: the eventual owner must obtain the
actual endpoint from a fresh receipt/source gate. withTransport additionally
refuses an IP change rather than reproducing the unrestricted Java setter.
The role/ID projection compares56 original constructor/getter/setter cases; no
network-position, packet-admission, actual router or NAT result is implied.
